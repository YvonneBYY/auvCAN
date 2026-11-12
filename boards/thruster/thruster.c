#include "thruster.h"

#include <string.h>

#include <uavcan.protocol.NodeStatus.h>
#include <uavcan.equipment.esc.RawCommand.h>
#include <cuauv.ThrusterStatus.h>

/* ------------------------------------------------------------------------- */
/* Pure helpers                                                               */
/* ------------------------------------------------------------------------- */

static int16_t clamp_orion(int32_t v)
{
    if (v > THRUSTER_MAX)  return THRUSTER_MAX;
    if (v < -THRUSTER_MAX) return -THRUSTER_MAX;
    return (int16_t)v;
}

int16_t thruster_from_raw(int16_t raw)
{
    return clamp_orion(raw / THRUSTER_RAW_SCALE);          /* C division truncates toward 0 */
}

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

int16_t thruster_ramp_step(int16_t cur, int16_t tgt, uint8_t inc, uint8_t dec)
{
    if (cur == tgt) return cur;

    /* Reversing: spin down to 0 first; the next step spins up the other way. */
    int32_t toward = tgt;
    if ((cur > 0 && tgt < 0) || (cur < 0 && tgt > 0)) toward = 0;

    const int32_t step = (iabs(toward) > iabs(cur)) ? inc : dec;   /* up : down */
    int32_t next;
    if (toward > cur) {
        next = cur + step;
        if (next > toward) next = toward;
    } else {
        next = cur - step;
        if (next < toward) next = toward;
    }
    return (int16_t)next;
}

uint16_t thruster_pulse_us(int16_t value)
{
    /* Orion desToPWM, in its 2 us timer ticks: 750 = 1500 us neutral,
     * 763 + floor(v/255*187) forward, 737 + floor(v/255*187) reverse. */
    const int32_t v = clamp_orion(value);
    if (v == 0) return THRUSTER_NEUTRAL_US;
    int32_t ticks;
    if (v > 0) {
        ticks = 763 + (v * 187) / 255;                     /* floor for v > 0 */
    } else {
        ticks = 737 - ((-v * 187) + 254) / 255;            /* floor for v < 0 */
    }
    return (uint16_t)(ticks * 2);
}

/* ------------------------------------------------------------------------- */
/* Outputs                                                                    */
/* ------------------------------------------------------------------------- */

static void write_pulse(thruster_t *t, uint8_t ch, uint16_t us)
{
    t->pulse_us[ch] = us;
    if (t->pwm.write_us != NULL) t->pwm.write_us(t->pwm.ctx, ch, us);
}

static void outputs_safe(thruster_t *t)
{
    for (uint8_t i = 0; i < THRUSTER_CHANNELS; i++) {
        t->current[i] = 0;
        t->target[i] = 0;
        write_pulse(t, i, THRUSTER_NEUTRAL_US);
    }
}

int16_t thruster_applied(const thruster_t *t, uint8_t ch)
{
    if (ch >= THRUSTER_CHANNELS || auvcan_is_killed(t->can)) return 0;
    return (int16_t)(t->cfg.polarity[ch] * t->current[ch]);   /* back to the command frame */
}

/* ------------------------------------------------------------------------- */
/* Callbacks                                                                  */
/* ------------------------------------------------------------------------- */

/* NodeStatus status code bit 0 = killed. The other bits belong to whatever else the
 * board does (the sensor board's leak and fault bits), so leave them alone. */
static void set_killed_bit(auvcan_t *can, bool killed)
{
    auvcan_set_status_code(can, (uint16_t)((can->status_code & ~THRUSTER_CODE_KILLED) |
                                           (killed ? THRUSTER_CODE_KILLED : 0u)));
}

bool thruster_warning(const thruster_t *t)
{
    return !auvcan_is_killed(t->can) && t->stale;         /* armed but no commands arriving */
}

void thruster_kill_cb(auvcan_t *can, bool killed, void *user)
{
    thruster_t *t = user;
    set_killed_bit(can, killed);
    if (t == NULL || t->can == NULL) return;              /* not initialised yet */
    if (killed) {
        outputs_safe(t);                                  /* neutral NOW, ramp state zeroed */
        t->kill_resets++;
    }
}

static void on_rawcmd(auvcan_t *can, CanardRxTransfer *tr, void *user)
{
    thruster_t *t = user;
    if (tr->source_node_id != t->cfg.cmd_source_node) {
        t->cmds_ignored++;
        return;
    }
    struct uavcan_equipment_esc_RawCommand msg;
    if (uavcan_equipment_esc_RawCommand_decode(tr, &msg) ||
        msg.cmd.len < (uint8_t)(t->cfg.cmd_offset + THRUSTER_CHANNELS)) {
        t->cmds_ignored++;                                /* malformed, or doesn't cover our slice */
        return;
    }
    for (uint8_t i = 0; i < THRUSTER_CHANNELS; i++) {
        const int16_t v = thruster_from_raw(msg.cmd.data[t->cfg.cmd_offset + i]);
        t->target[i] = clamp_orion((int32_t)v * t->cfg.polarity[i]);
    }
    auvcan_fresh_feed(&t->cmd_fresh, can->poll_now_us);
    t->cmds_accepted++;
}

/* ------------------------------------------------------------------------- */
/* Public                                                                     */
/* ------------------------------------------------------------------------- */

int thruster_init(thruster_t *t, auvcan_t *can, const thruster_cfg_t *cfg,
                  const thruster_pwm_t *pwm)
{
    if (t == NULL || can == NULL || cfg == NULL || pwm == NULL || cfg->ramp_period_ms == 0u) {
        return -1;
    }
    for (uint8_t i = 0; i < THRUSTER_CHANNELS; i++) {
        if (cfg->polarity[i] != 1 && cfg->polarity[i] != -1) return -1;
    }
    memset(t, 0, sizeof(*t));
    t->can = can;
    t->cfg = *cfg;
    t->pwm = *pwm;
    t->stale = true;
    auvcan_fresh_init(&t->cmd_fresh, AUVCAN_RAWCMD_STALE_MS);

    outputs_safe(t);                                      /* ESCs see neutral from boot */
    set_killed_bit(can, auvcan_is_killed(can));

    const uint64_t now = auvcan_micros(can);
    t->next_ramp_us = now;
    t->next_status_us = now;

    return auvcan_subscribe_hp(can, UAVCAN_EQUIPMENT_ESC_RAWCOMMAND_ID,
                               UAVCAN_EQUIPMENT_ESC_RAWCOMMAND_SIGNATURE, on_rawcmd, t);
}

static void ramp_tick(thruster_t *t)
{
    for (uint8_t i = 0; i < THRUSTER_CHANNELS; i++) {
        t->current[i] = thruster_ramp_step(t->current[i], t->target[i],
                                           t->cfg.ramp_inc, t->cfg.ramp_dec);
        write_pulse(t, i, thruster_pulse_us(t->current[i]));
    }
}

static void publish_status(thruster_t *t)
{
    struct cuauv_ThrusterStatus s;
    memset(&s, 0, sizeof(s));
    for (uint8_t i = 0; i < THRUSTER_CHANNELS; i++) s.applied[i] = thruster_applied(t, i);
    s.killed = auvcan_is_killed(t->can);
    s.stale = t->stale;
    uint8_t buf[CUAUV_THRUSTERSTATUS_MAX_SIZE];
    const uint32_t len = cuauv_ThrusterStatus_encode(&s, buf);
    auvcan_broadcast(t->can, CUAUV_THRUSTERSTATUS_SIGNATURE, CUAUV_THRUSTERSTATUS_ID,
                     &t->status_tid, AUVCAN_PRIO_TELEMETRY, buf, (uint16_t)len);
}

/* Fixed-rate schedule; after a stall, resync instead of bursting to catch up. */
static bool due(uint64_t *next, uint64_t now, uint32_t period_ms)
{
    if (now < *next) return false;
    *next += (uint64_t)period_ms * 1000u;
    if (now >= *next) *next = now + (uint64_t)period_ms * 1000u;
    return true;
}

void thruster_poll(thruster_t *t)
{
    const uint64_t now = t->can->poll_now_us;
    const bool killed = auvcan_is_killed(t->can);

    t->stale = !auvcan_fresh_ok(&t->cmd_fresh, now);
    if (t->stale) {
        for (uint8_t i = 0; i < THRUSTER_CHANNELS; i++) t->target[i] = 0;   /* ramp down at dec */
    }

    if (killed) {
        /* Belt and braces: the kill callback already did this on the edge. */
        for (uint8_t i = 0; i < THRUSTER_CHANNELS; i++) {
            t->current[i] = 0;
            if (t->pulse_us[i] != THRUSTER_NEUTRAL_US) write_pulse(t, i, THRUSTER_NEUTRAL_US);
        }
        (void)due(&t->next_ramp_us, now, t->cfg.ramp_period_ms);    /* keep the schedule moving */
    } else if (due(&t->next_ramp_us, now, t->cfg.ramp_period_ms)) {
        ramp_tick(t);
    }


    if (due(&t->next_status_us, now, THRUSTER_STATUS_PERIOD_MS)) {
        publish_status(t);
    }
}
