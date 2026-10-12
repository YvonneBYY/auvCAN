/*
 * board_config.h - bench controller: node 1, publishes cuauv.Kill.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "auvcan_spec.h"

/* Hardware profile. Pick ONE, or delete the include and set BOARD_* below by hand:
 *   hw_deft_controls_pcb.h   Deft Robotics Controls PCB (STM32G474RET6), CH1 / FDCAN1
 *   (none)                   STM32G4 Nucleo-64 defaults: FDCAN1 PA11/PA12, LED PA5, button PC13 */
#ifndef BOARD_NO_HW_PROFILE
#include "hw_deft_controls_pcb.h"
#endif

#define BOARD_NODE_ID         AUVCAN_NODE_CONTROLLER   /* must be 1: nodes only accept Kill from node 1 */
#define BOARD_NAME            "edu.cornell.cuauv.bench_controller"
#define BOARD_APP_NAME        "controller"
#define BOARD_HW_MAJOR        0
#define BOARD_HW_MINOR        1
#define BOARD_SW_MAJOR        0
#define BOARD_SW_MINOR        1
#ifndef BOARD_GIT_HASH
#define BOARD_GIT_HASH        0u
#endif

#ifndef BOARD_FDCAN_CLOCK_HZ
#define BOARD_FDCAN_CLOCK_HZ  24000000u
#endif

/* 0 = bench mode: no Jetson, host_ok is always true.
 * 1 = real mode: armed only while cuauv.HostHeartbeat from node 2 is fresh
 *     (AUVCAN_HOST_HB_TIMEOUT_MS) and the host isn't requesting soft kill. */
#ifndef BOARD_REQUIRE_HOST
#define BOARD_REQUIRE_HOST    0
#endif

#endif
