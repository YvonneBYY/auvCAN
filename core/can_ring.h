/*
 * can_ring.h - lock-free single-producer/single-consumer CAN frame ring.
 *
 * Ported from toucan/queue.c with three fixes:
 *   1. indices are volatile and published behind a release/acquire fence,
 *      so the ISR producer and main-loop consumer can't see a half-written slot;
 *   2. overflow drops the frame and counts it instead of hanging the MCU
 *      (toucan called wdt_kill() with the watchdog disabled -> infinite loop);
 *   3. capacity is a power of two, so wrap is a mask, not a modulo.
 *
 * Exactly ONE producer (the FDCAN RX ISR) and ONE consumer (auvcan_poll in the
 * main loop) per ring. No interrupt masking is needed under that rule.
 */
#ifndef AUVCAN_CAN_RING_H
#define AUVCAN_CAN_RING_H

#include <stdbool.h>
#include <stdint.h>
#include <canard.h>

#ifndef CAN_RING_CAPACITY
#define CAN_RING_CAPACITY 32u   /* must be a power of two; 32 x 16 B = 512 B */
#endif

#if (CAN_RING_CAPACITY & (CAN_RING_CAPACITY - 1u)) != 0u
#error "CAN_RING_CAPACITY must be a power of two"
#endif

typedef struct {
    CanardCANFrame slots[CAN_RING_CAPACITY];
    volatile uint32_t head;      /* written only by producer */
    volatile uint32_t tail;      /* written only by consumer */
    volatile uint32_t dropped;   /* frames lost to overflow (producer side) */
} can_ring_t;

void can_ring_init(can_ring_t *r);

/* Producer (ISR). Returns false and increments `dropped` if full. */
bool can_ring_push(can_ring_t *r, const CanardCANFrame *f);

/* Consumer (main loop). Returns false if empty. */
bool can_ring_pop(can_ring_t *r, CanardCANFrame *out);

uint32_t can_ring_count(const can_ring_t *r);

#endif
