/*
 * thruster.h - thruster board logic (one board, 8 thrusters), independent of the MCU.
 *
 * Ported from Orion's thrusters.c with three deliberate changes:
 *   1. On kill the outputs go to neutral AND the ramp state is zeroed, so
 *      re-arming ramps up from 0. Orion froze the ramp and stepped straight
 *      back to the pre-kill thrust on re-arm.
 *   2. ThrusterStatus.applied reads 0 while killed. Orion's readback kept
 *      reporting the frozen pre-kill value.
 *   3. inc/dec are magnitude-based: inc spins a thruster up, dec spins it
 *      down, in either direction. Orion's were signed (inc = towards +255),
 *      so slowing a reverse thruster used inc. Identical with the 5/5 defaults.
 *
 * Kept exactly: Orion units (-255..255), ramp of inc/dec per period, the
 * desToPWM pulse map (1500 us neutral, +/-26 us deadband, 1100..1900 us).
 *
 * Inputs : uavcan.equipment.esc.RawCommand from the host (node 2), 100 Hz;
 *          this board uses cmd[offset .. offset+7] (offset 0: T1-T8), each raw/32 -> Orion units.
 *          cuauv.Kill via the auvCAN core (auvcan_is_killed / on_kill_change).
 * Outputs: 8 PWM channels through thruster_pwm_t; cuauv.ThrusterStatus at 20 Hz;
 *          NodeStatus status code bit 0 = killed (other bits untouched);
 *          thruster_warning() = armed with a stale command stream.
 * Runs on the sensor board (node 30) since the thruster board was retired.
 *
 * Usage (see app.c):
 *     auvcan_init(&can, &cfg)   with cfg.on_kill_change = thruster_kill_cb,
 *                                    cfg.kill_user      = &thr
 *     thruster_init(&thr, &can, &tcfg, &pwm)     before auvcan_start()
 *     loop: auvcan_poll(&can); thruster_poll(&thr);
 */
#ifndef THRUSTER_H
#define THRUSTER_H

#include <stdbool.h>
#include <stdint.h>

#include "auvcan.h"

#define THRUSTER_CHANNELS     8
#define THRUSTER_MAX          255       /* Orion units */
#define THRUSTER_RAW_SCALE    32        /* RawCommand int14 = Orion units x 32 */
#define THRUSTER_NEUTRAL_US   1500u
#define THRUSTER_STATUS_PERIOD_MS 50u   /* 20 Hz */
#define THRUSTER_CODE_KILLED  0x0001u   /* NodeStatus status code bit owned by this module */

typedef struct {
    void *ctx;
    void (*write_us)(void *ctx, uint8_t channel, uint16_t pulse_us);
} thruster_pwm_t;

typedef struct {
    uint8_t cmd_offset;                       /* first RawCommand.cmd index this board uses (0) */
    int8_t  polarity[THRUSTER_CHANNELS];      /* +1 / -1 per output channel */
    uint8_t ramp_period_ms;                   /* Orion RampPeriod (10) */
    uint8_t ramp_inc;                         /* Orion RampInc (5): spin-up step */
    uint8_t ramp_dec;                         /* Orion RampDec (5): spin-down step */
    uint8_t cmd_source_node;                  /* only accept RawCommand from here (AUVCAN_NODE_HOST) */
} thruster_cfg_t;

typedef struct {
    auvcan_t        *can;
    thruster_cfg_t   cfg;
    thruster_pwm_t   pwm;

    auvcan_fresh_t   cmd_fresh;
    int16_t          target[THRUSTER_CHANNELS];   /* physical frame (polarity applied) */
    int16_t          current[THRUSTER_CHANNELS];  /* physical frame, after ramp */
    uint16_t         pulse_us[THRUSTER_CHANNELS]; /* last value written */
    bool             stale;

    uint64_t         next_ramp_us;
    uint64_t         next_status_us;
    uint8_t          status_tid;

    /* diagnostics */
    uint32_t         cmds_accepted;
    uint32_t         cmds_ignored;     /* wrong source, too short, or malformed */
    uint32_t         kill_resets;
} thruster_t;

int  thruster_init(thruster_t *t, auvcan_t *can, const thruster_cfg_t *cfg,
                   const thruster_pwm_t *pwm);
void thruster_poll(thruster_t *t);

/* auvcan_config_t.on_kill_change; kill_user must point at the thruster_t. */
void thruster_kill_cb(auvcan_t *can, bool killed, void *user);

/* True while armed with a stale command stream. The app turns this (with anything else
 * the board checks) into NodeStatus health WARNING; this module doesn't set health. */
bool thruster_warning(const thruster_t *t);

/* Value reported in ThrusterStatus.applied[ch] (command frame, 0 while killed). */
int16_t thruster_applied(const thruster_t *t, uint8_t ch);

/* ---- Pure helpers (exposed for unit tests) -------------------------------- */
int16_t  thruster_from_raw(int16_t raw);                       /* raw/32, clamped */
int16_t  thruster_ramp_step(int16_t cur, int16_t tgt, uint8_t inc, uint8_t dec);
uint16_t thruster_pulse_us(int16_t value);                     /* Orion desToPWM */

#endif
