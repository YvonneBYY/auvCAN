/*
 * auvcan_port_g4.h - STM32G4 FDCAN port for the auvCAN core.
 *
 * Classic CAN 2.0B only (no FD, no bit-rate switching), 29-bit IDs.
 *
 * What CubeMX still does for you (HAL_FDCAN_MspInit):
 *   - FDCAN kernel clock source  -> use HSE or a PLL derived from HSE, never HSI
 *   - FDCANx_RX / FDCANx_TX GPIO alternate function
 *   - NVIC: enable FDCANx_IT0_IRQn ONLY. All FDCAN interrupts use line 0, so a
 *           single ISR is the only producer for both RX rings. (If CubeMX also
 *           enables IT1 it is harmless: nothing is routed to line 1.)
 *
 * What this port does (do NOT also call MX_FDCANx_Init):
 *   - bit timing for AUVCAN_BITRATE at 87.5 % sample point from the kernel clock
 *   - acceptance filters from the core's subscription table
 *   - TX in queue mode (hardware always sends the highest-priority frame first)
 *   - RX ISR -> lock-free rings (rx_hp from FIFO0, rx from FIFO1; one ISR fills both)
 *   - bus-off recovery (FDCAN does NOT recover on its own)
 *
 * This port owns HAL_FDCAN_RxFifo0Callback, HAL_FDCAN_RxFifo1Callback and
 * HAL_FDCAN_ErrorStatusCallback. Don't define them elsewhere in the board.
 */
#ifndef AUVCAN_PORT_G4_H
#define AUVCAN_PORT_G4_H

#include "stm32g4xx_hal.h"
#include "auvcan_port.h"
#include "can_ring.h"

typedef struct {
    FDCAN_HandleTypeDef *hfdcan;
    uint32_t             kernel_clock_hz;

    can_ring_t           rx_hp;       /* filled from RX FIFO0 (high-priority filters) */
    can_ring_t           rx;          /* filled from RX FIFO1 (everything else)       */
    volatile uint32_t    hw_lost;     /* hardware FIFO overruns (ISR too slow) */
    volatile uint32_t    bus_off_count;

    uint32_t             last_tick_ms;
    uint64_t             tick_ext_ms;

    auvcan_port_t        port;
} auvcan_g4_t;

/*
 * Configure FDCAN for AUVCAN_BITRATE. Calls HAL_FDCAN_Init (and so your MspInit).
 *   instance:        FDCAN1 / FDCAN2 / FDCAN3
 *   kernel_clock_hz: the FDCAN kernel clock CubeMX selected (e.g. 16000000 for a 16 MHz HSE)
 * Returns 0 on success, -1 if no exact bit timing exists for that clock, -2 on HAL error.
 */
int auvcan_g4_init(auvcan_g4_t *g4, FDCAN_HandleTypeDef *hfdcan,
                   FDCAN_GlobalTypeDef *instance, uint32_t kernel_clock_hz);

/* Port vtable to put in auvcan_config_t.port */
const auvcan_port_t *auvcan_g4_port(auvcan_g4_t *g4);

#endif
