/*
 * board_config.h - bench node: an actuator that obeys cuauv.Kill.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "auvcan_spec.h"

/* Hardware profile, chosen by a preprocessor symbol:
 *   (default)                 hw_deft_controls_pcb.h  Deft Controls PCB (G474RET6), CH1 / FDCAN1
 *   BOARD_HW_NUCLEO_G431KB    hw_nucleo_g431kb.h      NUCLEO-G431KB + external transceiver
 *   BOARD_NO_HW_PROFILE       none: Nucleo-64 defaults (FDCAN1 PA11/PA12, LED PA5, button PC13) */
#if defined(BOARD_HW_NUCLEO_G431KB)
#include "hw_nucleo_g431kb.h"
#elif !defined(BOARD_NO_HW_PROFILE)
#include "hw_deft_controls_pcb.h"
#endif

#ifndef BOARD_NODE_ID
#define BOARD_NODE_ID         AUVCAN_NODE_BENCH        /* 10; any free ID (e.g. 11) for a second test node */
#endif
#define BOARD_NAME            "edu.cornell.cuauv.bench_node"
#define BOARD_APP_NAME        "node"
#define BOARD_HW_MAJOR        0
#define BOARD_HW_MINOR        1
#define BOARD_SW_MAJOR        0
#define BOARD_SW_MINOR        1
#ifndef BOARD_GIT_HASH
#define BOARD_GIT_HASH        0u
#endif

/* FDCAN kernel clock as set in CubeMX. Must come from HSE. Check which crystal
 * your Nucleo has fitted (many G4 Nucleo-64s use 24 MHz). */
#ifndef BOARD_FDCAN_CLOCK_HZ
#define BOARD_FDCAN_CLOCK_HZ  24000000u
#endif

#endif
