/*
 * board_config.h - the only file most boards need to edit to get on the bus.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "auvcan_spec.h"

#define BOARD_NODE_ID         AUVCAN_NODE_THRUSTER_A        /* pick from auvcan_spec.h */
#define BOARD_NAME            "edu.cornell.cuauv.template"
#define BOARD_HW_MAJOR        1
#define BOARD_HW_MINOR        0
#define BOARD_SW_MAJOR        0
#define BOARD_SW_MINOR        1
#ifndef BOARD_GIT_HASH
#define BOARD_GIT_HASH        0u      /* pass -DBOARD_GIT_HASH=0x$(git rev-parse --short=8 HEAD) */
#endif

/* FDCAN kernel clock as configured in CubeMX (Clock Configuration tab).
 * Must come from HSE (directly or via PLL). 16, 24, 80, 160, 170 MHz all work. */
#define BOARD_FDCAN_CLOCK_HZ  16000000u

/* Actuator boards (thrusters, anything that moves or switches power)
 * obey cuauv.Kill. Sensors, BMB, acoustics: set to 0. */
#define BOARD_IS_ACTUATOR     1

#endif
