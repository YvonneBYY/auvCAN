/*
 * board_config.h - bench controller: node 1, publishes cuauv.Kill.
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

/* 1 = bench throttle: the boss also publishes esc.RawCommand at 50 Hz from bench_cmd[],
 *     which you edit in the debugger (Live Expressions). Orion units, -255..255 per
 *     thruster, bench_cmd[0..7] = T1..T8. Sent only while armed; cleared
 *     to 0 on every kill. The thrusters must be built with THRUSTER_CMD_SOURCE=1 to accept
 *     it. BENCH ONLY: on the sub, thrust commands come from the host (node 2). */
#ifndef BOARD_BENCH_THROTTLE
#define BOARD_BENCH_THROTTLE  0
#endif

#endif
