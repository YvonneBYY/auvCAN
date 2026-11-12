#include "can_ring.h"

#define RING_MASK (CAN_RING_CAPACITY - 1u)

void can_ring_init(can_ring_t *r)
{
    r->head = 0u;
    r->tail = 0u;
    r->dropped = 0u;
}

bool can_ring_push(can_ring_t *r, const CanardCANFrame *f)
{
    const uint32_t head = r->head;
    const uint32_t tail = r->tail;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);   /* see consumer's tail before reusing the slot */

    if ((head - tail) >= CAN_RING_CAPACITY) {
        r->dropped++;
        return false;
    }
    r->slots[head & RING_MASK] = *f;
    __atomic_thread_fence(__ATOMIC_RELEASE);   /* slot contents visible before the new head */
    r->head = head + 1u;
    return true;
}

bool can_ring_pop(can_ring_t *r, CanardCANFrame *out)
{
    const uint32_t tail = r->tail;
    const uint32_t head = r->head;
    if (head == tail) {
        return false;
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);   /* read the slot only after seeing the head */
    *out = r->slots[tail & RING_MASK];
    __atomic_thread_fence(__ATOMIC_RELEASE);   /* finish reading before releasing the slot */
    r->tail = tail + 1u;
    return true;
}

uint32_t can_ring_count(const can_ring_t *r)
{
    return r->head - r->tail;
}
