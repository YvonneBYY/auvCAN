/*
 * bench_controller/app.c - node 1: decides armed/killed and publishes cuauv.Kill.
 *
 *   armed = !hard_kill && !soft_kill && host_ok
 *
 *   hard_kill  the button, as a latched toggle. Boots KILLED; press to arm,
 *              press again to kill. (On the sub this becomes the hard-kill
 *              switch input.)
 *   soft_kill  requested by the Jetson in cuauv.HostHeartbeat (BOARD_REQUIRE_HOST=1)
 *   host_ok    HostHeartbeat from node 2 seen within 500 ms (always true in bench mode)
 *
 * Publishes every 50 ms (20 Hz), and immediately whenever `armed` changes, so a
 * kill never waits for the next period.
 *
 *   LED on          publishing armed
 *   LED slow blink  publishing killed (2 Hz)
 *   LED fast blink  bus trouble: this board is error-passive (its frames aren't
 *                   being ACKed: wiring, transceiver standby, clock/bit rate).
 *                   Shown regardless of kill state.
 *
 * NodeStatus vendor_specific_status_code bit 0 = killed (what is being published).
 */
#include "app.h"

#include "board_config.h"
#include "bsp.h"
#include "auvcan.h"

#include <cuauv.Kill.h>
#if BOARD_REQUIRE_HOST
#include <cuauv.HostHeartbeat.h>
#endif

#define DEBOUNCE_US        30000u
#define SLOW_BLINK_HALF_US 250000u
#define FAST_BLINK_HALF_US  50000u
#define STATS_PERIOD_US    100000u

static auvcan_t can;

/* published state */
static uint8_t  kill_tid;
static uint8_t  kill_seq;
static bool     last_armed;
static uint64_t next_kill_us;

/* inputs */
static bool     hard_kill = true;        /* boots killed: a controller reset never arms anything */
static bool     btn_raw, btn_stable;
static uint64_t btn_changed_us;

/* diagnostics */
static bool     bus_trouble;
static uint64_t next_stats_us;

#if BOARD_REQUIRE_HOST
static auvcan_fresh_t host_fresh;
static bool           host_soft_kill = true;

static void on_host_heartbeat(auvcan_t *c, CanardRxTransfer *t, void *user)
{
    (void)user;
    struct cuauv_HostHeartbeat hb;
    if (cuauv_HostHeartbeat_decode(t, &hb)) return;
    if (t->source_node_id != AUVCAN_NODE_HOST) return;              /* only the Jetson */
    if (auvcan_fresh_feed_seq(&host_fresh, c->poll_now_us, hb.seq)) {
        host_soft_kill = hb.soft_kill;
    }
}
#endif

static void publish_kill(bool armed, bool host_ok, bool soft_kill)
{
    struct cuauv_Kill k = {
        .armed     = armed,
        .hard_kill = hard_kill,
        .soft_kill = soft_kill,
        .host_ok   = host_ok,
        .seq       = kill_seq++,
    };
    uint8_t buf[CUAUV_KILL_MAX_SIZE];
    const uint32_t len = cuauv_Kill_encode(&k, buf);
    auvcan_broadcast(&can, CUAUV_KILL_SIGNATURE, CUAUV_KILL_ID, &kill_tid,
                     AUVCAN_PRIO_KILL, buf, (uint16_t)len);
    last_armed = armed;
    auvcan_set_status_code(&can, (uint16_t)(armed ? 0u : 1u));
}

static void poll_button(uint64_t now)
{
    const bool raw = bsp_button();
    if (raw != btn_raw) {                       /* edge: restart the debounce window */
        btn_raw = raw;
        btn_changed_us = now;
    } else if (raw != btn_stable && (now - btn_changed_us) >= DEBOUNCE_US) {
        btn_stable = raw;
        if (raw) {
            hard_kill = !hard_kill;             /* toggle on press */
        }
    }
}

void app_setup(void)
{
    const auvcan_port_t *port = bsp_init();
    if (port == NULL) bsp_fatal("bsp_init: CAN port (check BOARD_FDCAN_CLOCK_HZ)");
    bsp_led(false);

    const auvcan_config_t cfg = {
        .node_id         = BOARD_NODE_ID,
        .name            = BOARD_NAME,
        .hw_major        = BOARD_HW_MAJOR,
        .hw_minor        = BOARD_HW_MINOR,
        .sw_major        = BOARD_SW_MAJOR,
        .sw_minor        = BOARD_SW_MINOR,
        .sw_vcs_commit   = BOARD_GIT_HASH,
        .kill_timeout_ms = 0,                   /* the controller publishes Kill; it doesn't obey it */
        .port            = port,
    };
    if (auvcan_init(&can, &cfg) != 0) bsp_fatal("auvcan_init");

#if BOARD_REQUIRE_HOST
    auvcan_fresh_init(&host_fresh, AUVCAN_HOST_HB_TIMEOUT_MS);
    if (auvcan_subscribe(&can, CUAUV_HOSTHEARTBEAT_ID, CUAUV_HOSTHEARTBEAT_SIGNATURE,
                         on_host_heartbeat, NULL) != 0) bsp_fatal("subscribe HostHeartbeat");
#endif

    auvcan_set_status_code(&can, 1u);
    if (auvcan_start(&can) != 0) bsp_fatal("auvcan_start");
    next_kill_us = auvcan_micros(&can);         /* first Kill on the first loop */
}

void app_loop(void)
{
    auvcan_poll(&can);
    const uint64_t now = auvcan_micros(&can);

    poll_button(now);

#if BOARD_REQUIRE_HOST
    const bool host_ok   = auvcan_fresh_ok(&host_fresh, now);
    const bool soft_kill = host_soft_kill;
#else
    const bool host_ok   = true;
    const bool soft_kill = false;
#endif
    const bool armed = !hard_kill && !soft_kill && host_ok;

    /* 20 Hz, plus immediately on any change of `armed` */
    if (now >= next_kill_us || armed != last_armed) {
        publish_kill(armed, host_ok, soft_kill);
        next_kill_us += (uint64_t)AUVCAN_KILL_PERIOD_MS * 1000u;
        if (now >= next_kill_us) {
            next_kill_us = now + (uint64_t)AUVCAN_KILL_PERIOD_MS * 1000u;   /* resync after a stall */
        }
    }

    if (now >= next_stats_us) {
        next_stats_us = now + STATS_PERIOD_US;
        auvcan_bus_stats_t st;
        auvcan_get_bus_stats(&can, &st);
        bus_trouble = st.error_passive;
    }

    if (bus_trouble) {
        bsp_led(((now / FAST_BLINK_HALF_US) & 1u) != 0u);
    } else {
        bsp_led(last_armed ? true : (((now / SLOW_BLINK_HALF_US) & 1u) != 0u));
    }
}
