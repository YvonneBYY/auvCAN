/*
 * auvcan_spec.h - system-wide constants from the auvCAN spec v0.
 * Change these here, and only here.
 */
#ifndef AUVCAN_SPEC_H
#define AUVCAN_SPEC_H

/* ---- Bus ---------------------------------------------------------------- */
#define AUVCAN_BITRATE                 1000000u   /* CAN 2.0B, classic frames */
#define AUVCAN_SAMPLE_POINT_PERMILLE   875u       /* 87.5 % (DroneCAN recommendation) */

/* ---- Node IDs (static, unique across the vehicle) ------------------------ */
#define AUVCAN_NODE_CONTROLLER   1   /* power board: reads the hard-kill switch, publishes cuauv.Kill,
                                        and sends the power telemetry (PowerStatus, RailCurrents) */
#define AUVCAN_NODE_POWER        AUVCAN_NODE_CONTROLLER   /* same board, same node ID */
#define AUVCAN_NODE_HOST         2   /* Jetson host node, on the bus through a USB-CAN interface */
#define AUVCAN_NODE_DEBUG        3   /* laptop through the auv_controller gateway: debug / backup
                                        access; may request an operator kill, never thrust or arm */
/* 10 (thruster board) and 11 (Thruster B) retired: the sensor board (30) drives all
 * 8 ESCs (T1-T8 = RawCommand.cmd[0..7]). */
#define AUVCAN_NODE_BENCH       10   /* bench/test boards only (bench_node, unit tests); not on the sub */
#define AUVCAN_NODE_BMB         20
/* 21 (separate power-distribution node) retired: the power board is node 1. */
#define AUVCAN_NODE_SENSOR      30   /* depth, hull, leaks AND the 8 thruster ESC outputs */
#define AUVCAN_NODE_THRUSTER    AUVCAN_NODE_SENSOR   /* the node that drives the thrusters */
#define AUVCAN_NODE_ACOUSTICS   31

/* ---- Priority classes (0 = highest, 31 = lowest) ------------------------ */
#define AUVCAN_PRIO_KILL          0
#define AUVCAN_PRIO_HULL_EVENT    2   /* leak state change */
#define AUVCAN_PRIO_ACTUATOR      4   /* esc.RawCommand */
#define AUVCAN_PRIO_HOST_HB       6   /* cuauv.HostHeartbeat */
#define AUVCAN_PRIO_SENSOR_FAST   8   /* cuauv.Depth */
#define AUVCAN_PRIO_TELEMETRY    12   /* esc.Status, power.CircuitStatus */
#define AUVCAN_PRIO_TELEM_SLOW   14   /* cuauv.HullStatus periodic, PingResult */
#define AUVCAN_PRIO_NODESTATUS   16
#define AUVCAN_PRIO_SERVICE      20   /* GetNodeInfo, param, RestartNode */
#define AUVCAN_PRIO_DEBUG        28

/* ---- Timeouts (defaults; become params in stage 3) ----------------------- */
#define AUVCAN_KILL_TIMEOUT_MS          250u
#define AUVCAN_RAWCMD_STALE_MS          200u
#define AUVCAN_HOST_HB_TIMEOUT_MS       500u

/* ---- Rates --------------------------------------------------------------- */
#define AUVCAN_KILL_PERIOD_MS            50u   /* 20 Hz */
#define AUVCAN_HOST_HB_PERIOD_MS        100u   /* 10 Hz */
#define AUVCAN_NODESTATUS_PERIOD_MS    1000u   /*  1 Hz */

#endif
