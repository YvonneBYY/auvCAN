/*
 * auvcan_port.h - what a hardware port must provide to the auvCAN core.
 *
 * Implementations:
 *   ports/stm32g4/auvcan_port_g4.c  - STM32G4 FDCAN (classic CAN 2.0B)
 *   ports/sim/auvcan_port_sim.c     - in-process virtual bus for host tests
 *
 * The core never touches hardware directly, so the protocol logic
 * (node services, kill watchdog, dispatch) is unit-tested on a PC.
 */
#ifndef AUVCAN_PORT_H
#define AUVCAN_PORT_H

#include <stdbool.h>
#include <stdint.h>
#include <canard.h>

/* One hardware acceptance filter: 29-bit id/mask, classic mask mode. */
typedef struct {
    uint32_t id;
    uint32_t mask;
    bool     high_priority;   /* route to the fast lane (G4: RX FIFO0 + HP flag) */
} auvcan_filter_t;

typedef struct {
    uint32_t rx_overflow;     /* frames dropped because the RX ring was full */
    uint32_t tx_errors;       /* frames the port rejected and the core dropped */
    uint32_t bus_off_count;   /* times the controller went bus-off */
    uint8_t  tec;             /* transmit error counter (live) */
    uint8_t  rec;             /* receive error counter (live) */
    bool     error_passive;
} auvcan_bus_stats_t;

typedef struct {
    void *ctx;

    /* Apply filters and go on-bus. Called once from auvcan_start().
     * If n_filters exceeds the hardware's capacity, the port must fall back to
     * accepting all extended frames (the core filters in software anyway). */
    int  (*start)(void *ctx, const auvcan_filter_t *filters, uint8_t n_filters);

    /* Queue one frame for transmission.
     * Returns 1 = accepted, 0 = hardware TX full (retry later), <0 = error (drop). */
    int  (*tx)(void *ctx, const CanardCANFrame *frame);

    /* Pop one received frame (filled by the RX ISR). Returns false if none. */
    bool (*rx)(void *ctx, CanardCANFrame *frame);

    /* Monotonic microseconds. Main-loop context only. */
    uint64_t (*micros)(void *ctx);

    /* 16-byte unique ID for GetNodeInfo (STM32: 96-bit UID, zero-padded). */
    void (*unique_id)(void *ctx, uint8_t out[16]);

    void (*get_stats)(void *ctx, auvcan_bus_stats_t *out);

    /* Reset the MCU. Must not return on hardware. */
    void (*reboot)(void *ctx);
} auvcan_port_t;

#endif
