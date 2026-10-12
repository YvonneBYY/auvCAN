/*
 * bench_node/app.c - an actuator node whose LED shows its kill state.
 *
 *   LED on          armed   (fresh cuauv.Kill(armed) from node 1)
 *   LED off         killed  (boot, Kill(killed), or no Kill for 250 ms)
 *   LED fast blink  bus trouble: controller error-passive (no ACKs, wiring,
 *                   wrong bit rate). Shown regardless of kill state.
 *
 * NodeStatus vendor_specific_status_code bit 0 = killed, so `bench.py monitor`
 * and the DroneCAN GUI Tool show the same state as the LED.
 *
 * On a real actuator board, replace the LED with outputs: drive them only while
 * !auvcan_is_killed(), and put them in their safe state in on_kill_change().
 */
#include "app.h"

#include "board_config.h"
#include "bsp.h"
#include "auvcan.h"

#define STATS_PERIOD_US   100000u     /* re-read bus health every 100 ms */
#define BLINK_HALF_US      50000u     /* 10 Hz blink for bus trouble */

static auvcan_t can;
static bool     bus_trouble;
static uint64_t next_stats_us;

static void on_kill_change(auvcan_t *c, bool killed, void *user)
{
    (void)user;
    /* Real board: outputs to safe state here (ESC neutral, rails off). */
    auvcan_set_status_code(c, (uint16_t)(killed ? 1u : 0u));
}

void app_setup(void)
{
    const auvcan_port_t *port = bsp_init();
    if (port == NULL) bsp_fatal("bsp_init: CAN port (check BOARD_FDCAN_CLOCK_HZ)");

    bsp_led(false);     /* on_kill_change() is not called at boot: set the safe state ourselves */

    const auvcan_config_t cfg = {
        .node_id         = BOARD_NODE_ID,
        .name            = BOARD_NAME,
        .hw_major        = BOARD_HW_MAJOR,
        .hw_minor        = BOARD_HW_MINOR,
        .sw_major        = BOARD_SW_MAJOR,
        .sw_minor        = BOARD_SW_MINOR,
        .sw_vcs_commit   = BOARD_GIT_HASH,
        .kill_timeout_ms = AUVCAN_KILL_TIMEOUT_MS,
        .on_kill_change  = on_kill_change,
        .port            = port,
    };
    if (auvcan_init(&can, &cfg) != 0) bsp_fatal("auvcan_init");
    auvcan_set_status_code(&can, 1u);                     /* killed */
    if (auvcan_start(&can) != 0) bsp_fatal("auvcan_start");
}

void app_loop(void)
{
    auvcan_poll(&can);

    const uint64_t now = auvcan_micros(&can);
    if (now >= next_stats_us) {
        next_stats_us = now + STATS_PERIOD_US;
        auvcan_bus_stats_t st;
        auvcan_get_bus_stats(&can, &st);
        bus_trouble = st.error_passive;
    }

    if (bus_trouble) {
        bsp_led(((now / BLINK_HALF_US) & 1u) != 0u);
    } else {
        bsp_led(!auvcan_is_killed(&can));
    }
}
