/*
 * sensor/app.c - the sensor board (node 30): sensors AND the 8 thruster ESC outputs.
 * Same file for the G4 build (bsp_g4.c + hw_g4.c + pwm_g4.c) and the Linux build
 * (bsp_linux.c + hw_linux.c + pwm_linux.c).
 *
 * The two jobs are independent modules on one auvCAN node:
 *   sensor.c   (boards/sensor)    Depth, HullStatus, leaks; never affected by Kill
 *   thruster.c (boards/thruster)  obeys cuauv.Kill from node 1 and RawCommand from node 2,
 *                                 drives the PWM, publishes ThrusterStatus
 * NodeStatus:
 *   status code  bit 0 killed (thruster.c), bits 1-4 leaks, bit 5 depth fault, bit 6
 *                internal-temp fault, bit 7 hull-pressure fault (sensor.c)
 *   health       WARNING if a sensor has failed or the thrusters are armed but stale
 *
 *   LED 2 Hz blink  a leak input is active (overrides the rest)
 *   LED off         a sensor has failed
 *   LED on          sensors OK
 * The heartbeat LED shows the firmware is running; ThrusterStatus / the status line
 * show armed or killed.
 */
#include "app.h"

#include "board_config.h"
#include "bsp.h"
#include "pwm.h"
#include "sensor.h"
#include "sensor_hw.h"
#include "thruster.h"
#include "auvcan.h"
#include "statuslog.h"

#include <uavcan.protocol.NodeStatus.h>

#include <stdarg.h>
#include <stdio.h>

static auvcan_t   can;
static sensor_t   sens;
static thruster_t thr;

__attribute__((format(printf, 4, 5)))
static size_t append(char *buf, size_t size, size_t n, const char *fmt, ...)
{
    if (n + 1u >= size) return n;
    va_list ap;
    va_start(ap, fmt);
    const int k = vsnprintf(buf + n, size - n, fmt, ap);
    va_end(ap);
    if (k < 0) return n;
    n += (size_t)k;
    return (n < size) ? n : size - 1u;
}

/* value with `decimals` digits, as integers: newlib-nano's printf has no %f by default */
static size_t append_fixed(char *buf, size_t size, size_t n, float v, unsigned decimals)
{
    long scale = 1;
    for (unsigned i = 0; i < decimals; i++) scale *= 10;
    long x = (long)(v * (float)scale + (v < 0 ? -0.5f : 0.5f));
    const char *sign = (x < 0) ? "-" : "";
    if (x < 0) x = -x;
    return append(buf, size, n, "%s%ld.%0*ld", sign, x / scale, (int)decimals, x % scale);
}

/* Status line: what the sensors read right now. */
static size_t sensor_status(char *buf, size_t size, void *user)
{
    const sensor_t *s = user;
    size_t n = 0;
    if (s->depth_ok) {
        n = append(buf, size, n, "depth ");
        n = append_fixed(buf, size, n, s->depth_m, 3);
        n = append(buf, size, n, " m water ");
        n = append_fixed(buf, size, n, s->water_temp_c, 1);
        n = append(buf, size, n, " C");
    } else {
        n = append(buf, size, n, "depth FAULT");
    }
    if (s->hull_ok) {
        n = append(buf, size, n, " | hull ");
        n = append_fixed(buf, size, n, s->hull_atm, 3);
        n = append(buf, size, n, " atm");
    } else {
        n = append(buf, size, n, " | hull FAULT (raw %u)", (unsigned)s->hull_raw);
    }
    if (s->temp_ok) {
        n = append(buf, size, n, " int ");
        n = append_fixed(buf, size, n, s->internal_temp_c, 2);
        n = append(buf, size, n, " C");
    } else {
        n = append(buf, size, n, " int FAULT");
    }
    n = append(buf, size, n, " | leak %u%u%u%u", s->leak_stable & 1u, (s->leak_stable >> 1) & 1u,
               (s->leak_stable >> 2) & 1u, (s->leak_stable >> 3) & 1u);
    n = append(buf, size, n, " | i2c err %lu crc err %lu early %lu", (unsigned long)s->i2c_errors,
               (unsigned long)s->crc_errors, (unsigned long)s->adc_early);

    /* thrusters: armed/killed, the 8 pulse widths, commands getting through */
    static uint32_t last_ok, last_ign;
    const uint32_t ok  = thr.cmds_accepted - last_ok;
    const uint32_t ign = thr.cmds_ignored - last_ign;
    last_ok  = thr.cmds_accepted;
    last_ign = thr.cmds_ignored;
    n = append(buf, size, n, " | %s pwm", auvcan_is_killed(&can) ? "KILLED" : "ARMED");
    for (uint8_t i = 0; i < THRUSTER_CHANNELS; i++) n = append(buf, size, n, " %u", (unsigned)thr.pulse_us[i]);
    return append(buf, size, n, "%s cmd %lu/s ign %lu/s", thr.stale ? " STALE" : "",
                  (unsigned long)ok, (unsigned long)ign);
}

void app_setup(void)
{
    /* ESC outputs first: they see neutral before anything else can happen. */
    const thruster_pwm_t *pwm = pwm_init();
    if (pwm == NULL) bsp_fatal("pwm_init");

    const auvcan_port_t *port = bsp_init();
    if (port == NULL) bsp_fatal("bsp_init: CAN port (check BOARD_FDCAN_CLOCK_HZ)");
    bsp_led(false);

    const sensor_hw_t *hw = sensor_hw_init();
    if (hw == NULL) bsp_fatal("sensor_hw_init");

    const auvcan_config_t cfg = {
        .node_id         = BOARD_NODE_ID,
        .name            = BOARD_NAME,
        .hw_major        = BOARD_HW_MAJOR,
        .hw_minor        = BOARD_HW_MINOR,
        .sw_major        = BOARD_SW_MAJOR,
        .sw_minor        = BOARD_SW_MINOR,
        .sw_vcs_commit   = BOARD_GIT_HASH,
        .kill_timeout_ms = AUVCAN_KILL_TIMEOUT_MS,   /* the thrusters obey Kill (sensors don't care) */
        .on_kill_change  = thruster_kill_cb,
        .kill_user       = &thr,
        .port            = port,
    };
    if (auvcan_init(&can, &cfg) != 0) bsp_fatal("auvcan_init");

    const sensor_cfg_t scfg = {
        .model         = SENSOR_MS5837_MODEL,
        .water_density = SENSOR_WATER_DENSITY,
        .surface_pa    = SENSOR_SURFACE_PA,
        .mcp9808_addr  = SENSOR_MCP9808_ADDR,
    };
    if (sensor_init(&sens, &can, &scfg, hw) != 0) bsp_fatal("sensor_init");

    const thruster_cfg_t tcfg = {
        .cmd_offset      = THRUSTER_CMD_OFFSET,
        .polarity        = THRUSTER_POLARITY,
        .ramp_period_ms  = THRUSTER_RAMP_PERIOD_MS,
        .ramp_inc        = THRUSTER_RAMP_INC,
        .ramp_dec        = THRUSTER_RAMP_DEC,
        .cmd_source_node = THRUSTER_CMD_SOURCE,
    };
    if (thruster_init(&thr, &can, &tcfg, pwm) != 0) bsp_fatal("thruster_init");
    statuslog_init(&can);
    statuslog_set_extra(sensor_status, &sens);
    if (auvcan_start(&can) != 0) bsp_fatal("auvcan_start");
}

void app_loop(void)
{
    auvcan_poll(&can);
    thruster_poll(&thr);                       /* first: ramp / stale / kill before any I2C */
    sensor_poll(&sens);
    statuslog_poll(&can);
    auvcan_set_health(&can, (sensor_warning(&sens) || thruster_warning(&thr))
                                ? UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING
                                : UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK);

    const uint64_t now = can.poll_now_us;
    bsp_heartbeat(now);
    if (sens.leak_stable != 0u) bsp_led(((now / 250000u) & 1u) != 0u);
    else                        bsp_led(!sensor_warning(&sens));
}
