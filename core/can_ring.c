/*
Mailbox between the CAN receive interrupt and the main loop: the ISR drops received frames in, and auvcan_poll() takes them out. (ported from toucan's queue.c)

G4 port needs two rings, rx_hp (FIFO0) and rx (FIFO1). The two FIFOs raise two different interrupts at different priorities, and the FIFO0 ISR can preempt the FIFO1 ISR.

Rules:
    1. One producer and one consumer per ring. DONT push from two ISRs or pop from both an ISR and the main loop.
    2. Only the port calls push (in the ISR), and only auvcan_poll() calls pop (through port->rx()). Board code never touches the ring directly.

If the main loop falls more than 32 frames behind, push drops the new frame and increments dropped. 
Dropping should be okay (?) since 
- DroneCAN tolerates loss, missing Kill is covered by the watchdog
- Dropped feeds rx_overflow in the bus stats, so NodeStatus goes WARNING, Jetson also know that the
    node is being slow
*/

#include "can_ring.h"

#define RING_MASK (CAN_RING_CAPACITY - 1u)

void can_ring_init(can_ring_t *r)
{
    r->head = 0u; // total frames ever pushed  (written ONLY by the ISR)
    r->tail = 0u; // total frames ever popped  (written ONLY by the main loop)
    r->dropped = 0u;  // frames lost because the ring was full
}

/*
ISR side: 
*/
bool can_ring_push(can_ring_t *r, const CanardCANFrame *f)
{
    const uint32_t head = r->head;
    const uint32_t tail = r->tail;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);  // prevent slot write being done before tail is read

    if ((head - tail) >= CAN_RING_CAPACITY) { // full?
        r->dropped++;  //count it so no hang
        return false;
    }
    r->slots[head & RING_MASK] = *f; // write the frame
    __atomic_thread_fence(__ATOMIC_RELEASE);   // prevent head++ being done before the slot write
    r->head = head + 1u;   // publish it
    return true;
}

/*
Main loop side
*/
bool can_ring_pop(can_ring_t *r, CanardCANFrame *out)
{
    const uint32_t tail = r->tail;
    const uint32_t head = r->head;
    if (head == tail) {    // empty
        return false;
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);   //prevent reading the slot before reading head
    *out = r->slots[tail & RING_MASK];  // copy the frame out
    __atomic_thread_fence(__ATOMIC_RELEASE);   //prevent tail++ being done before the copy out
    r->tail = tail + 1u;     // free the slot
    return true;
}

uint32_t can_ring_count(const can_ring_t *r)
{
    return r->head - r->tail;
}
