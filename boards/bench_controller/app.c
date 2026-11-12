/*
 * bench_controller/app.c - node 1: decides armed/killed and publishes cuauv.Kill.
 *
 *   armed = !hard_kill && !soft_kill && host_ok && !node_kill
 *
 *   hard_kill  the button, as a latched toggle. Boots KILLED; press to arm,
 *              press again to kill. (On the sub this becomes the hard-kill
 *              switch input.)
 *   soft_kill  requested by the Jetson in cuauv.HostHeartbeat (BOARD_REQUIRE_HOST=1)
 *   host_ok    HostHeartbeat from node 2 seen within 500 ms (always true in bench mode)
 *   node_kill  a cuauv.KillRequest is in effect:
 *              - from node 3 (operator, via the auv_controller gateway): LATCHED. Clears
 *                when the hard-kill switch is put to killed, or on KillRequest(active=false)
 *                from node 3 while the switch is at killed (ignored otherwise). Either way
 *                arming takes a physical switch flip: the gateway can kill, never arm.
 *              - from any other node (e.g. BMB fault): while its requests keep coming
 *                (timeout 1.5 s), not latched.
 *              A KillRequest can only kill, never arm.
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
#include "statuslog.h"

#include <stdio.h>
#include <cuauv.Kill.h>
#include <cuauv.KillRequest.h>
#if BOARD_REQUIRE_HOST
#include <cuauv.HostHeartbeat.h>
#endif
#if BOARD_BENCH_THROTTLE
#include <uavcan.equipment.esc.RawCommand.h>
#endif

#define DEBOUNCE_US        30000u
#define SLOW_BLINK_HALF_US 250000u
#define FAST_BLINK_HALF_US  50000u
#define STATS_PERIOD_US    100000u

static auvcan_t can;

/* published state */
static uint8_t  kill_tid; // libcanard transfer ID, one per message type, transfer layer
static uint8_t  kill_seq; //unchanging seq -- stop counting the message as fresh, timeout
static bool     last_armed;
static uint64_t next_kill_us;

/* inputs */
static bool     hard_kill = true;        /* boots killed: a controller reset never arms anything */
static bool     btn_raw, btn_stable;
static uint64_t btn_changed_us;

/* diagnostics */
static bool     bus_trouble;
static uint64_t next_stats_us;

/* ---- node kills (cuauv.KillRequest) ---- */
#define NODE_KILL_TIMEOUT_US  1500000u     /* non-latching requests come at 2 Hz */
#define NODE_KILL_SLOTS       4u

static bool op_latched;                    /* operator kill from node 3, latched */
static struct { uint8_t id; bool active; uint64_t last_us; } nk[NODE_KILL_SLOTS];

static void on_kill_request(auvcan_t *c, CanardRxTransfer *t, void *user)
{
    (void)user;
    struct cuauv_KillRequest kr;
    if (cuauv_KillRequest_decode(t, &kr)) return;
    const uint8_t src = t->source_node_id;

    if (src == AUVCAN_NODE_DEBUG) {        /* operator: latch on; clear only with the switch at killed */
        if (kr.active) {
            op_latched = true;
        } else if (hard_kill) {
            op_latched = false;
        }
        return;
    }
    unsigned free_slot = NODE_KILL_SLOTS;
    for (unsigned i = 0; i < NODE_KILL_SLOTS; i++) {
        if (nk[i].id == src) { free_slot = i; break; }
        if (nk[i].id == 0u && free_slot == NODE_KILL_SLOTS) free_slot = i;
    }
    if (free_slot == NODE_KILL_SLOTS) return;              /* more than 4 requesters */
    nk[free_slot].id = src;
    nk[free_slot].active = kr.active;
    nk[free_slot].last_us = c->poll_now_us;
}

/* Returns the node ID whose request is in effect (operator first), 0 = none. */
static uint8_t node_kill_src(uint64_t now)
{
    if (op_latched) return AUVCAN_NODE_DEBUG;
    for (unsigned i = 0; i < NODE_KILL_SLOTS; i++) {
        if (nk[i].id != 0u && nk[i].active && (now - nk[i].last_us) < NODE_KILL_TIMEOUT_US) {
            return nk[i].id;
        }
    }
    return 0u;
}

static bool    st_soft, st_host;          /* last inputs, for the status line */
static uint8_t st_nk;

#if BOARD_BENCH_THROTTLE
/* Edit in Live Expressions while armed: Orion units -255..255 (100 = ~1670 us). */
#define BENCH_N           8                 /* T1..T8 */
volatile int16_t bench_cmd[BENCH_N];
static uint8_t   bench_tid;
static uint64_t  next_bench_us;
#define BENCH_PERIOD_US   20000u            /* 50 Hz, well inside the thruster's 200 ms stale limit */
#define BENCH_RAW_SCALE   32                /* Orion units -> RawCommand (-8192..8191) */


static void publish_bench_cmd(bool armed)
{
    struct uavcan_equipment_esc_RawCommand rc = { .cmd = { .len = BENCH_N } };
    for (uint8_t i = 0; i < BENCH_N; i++) {
        int32_t v = armed ? bench_cmd[i] : 0;
        if (v > 255) v = 255;
        if (v < -255) v = -255;
        rc.cmd.data[i] = (int16_t)(v * BENCH_RAW_SCALE);
    }
    uint8_t buf[UAVCAN_EQUIPMENT_ESC_RAWCOMMAND_MAX_SIZE];
    const uint32_t len = uavcan_equipment_esc_RawCommand_encode(&rc, buf);
    auvcan_broadcast(&can, UAVCAN_EQUIPMENT_ESC_RAWCOMMAND_SIGNATURE, UAVCAN_EQUIPMENT_ESC_RAWCOMMAND_ID,
                     &bench_tid, AUVCAN_PRIO_ACTUATOR, buf, (uint16_t)len);
}
#endif

#if BOARD_REQUIRE_HOST
static auvcan_fresh_t host_fresh;
static bool           host_soft_kill = true;

//2 checks before trusting the message
static void on_host_heartbeat(auvcan_t *c, CanardRxTransfer *t, void *user) 
{
    (void)user;
    struct cuauv_HostHeartbeat hb;
    if (cuauv_HostHeartbeat_decode(t, &hb)) return; //malformed
    if (t->source_node_id != AUVCAN_NODE_HOST) return; //only jetson
    //c->poll_now_us: timestamp the core read every start of poll, so every handler and the watchdog agree on “now”.
    if (auvcan_fresh_feed_seq(&host_fresh, c->poll_now_us, hb.seq)) { 
        host_soft_kill = hb.soft_kill;
    }
}
#endif

static size_t ctrl_status(char *buf, size_t size, void *user)
{
    (void)user;
    int k = snprintf(buf, size, "hard %d soft %d host_ok %d node_kill %u%s",
                     hard_kill, st_soft, st_host, (unsigned)st_nk,
                     op_latched ? " (operator, LATCHED)" : "");
#if BOARD_BENCH_THROTTLE
    if (k >= 0 && (size_t)k < size) {
        k += snprintf(buf + k, size - (size_t)k, " | bench_cmd");
        for (uint8_t i = 0; i < BENCH_N && k >= 0 && (size_t)k < size; i++) {
            k += snprintf(buf + k, size - (size_t)k, " %d", bench_cmd[i]);
        }
    }
#endif
    if (k < 0) return 0;
    return ((size_t)k < size) ? (size_t)k : size - 1u;
}

static void publish_kill(bool armed, bool host_ok, bool soft_kill, uint8_t nk_src)
{
    struct cuauv_Kill k = { //generated cuauv.Kill.h
        .armed     = armed,
        .hard_kill = hard_kill,
        .soft_kill = soft_kill,
        .host_ok   = host_ok,
        .node_kill     = nk_src != 0u,
        .node_kill_src = nk_src,
        .seq       = kill_seq++, //uses the current value for freshness
    };
    uint8_t buf[CUAUV_KILL_MAX_SIZE];
    const uint32_t len = cuauv_Kill_encode(&k, buf); //packs bit fields into 3 bytes, return length
    auvcan_broadcast(&can, CUAUV_KILL_SIGNATURE, CUAUV_KILL_ID, &kill_tid,
                     AUVCAN_PRIO_KILL, buf, (uint16_t)len); //broadcast kill with priority 0
    last_armed = armed; //record last armed
    auvcan_set_status_code(&can, (uint16_t)(armed ? 0u : 1u)); //mirror arm state into NodeStatus
}

//account for debouncing in actual mechanical contact
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
            if (hard_kill) op_latched = false;  /* switch back to killed clears an operator kill */
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
        .kill_timeout_ms = 0,                   // controller publishes Kill; doesn't obey
        .port            = port,
    };
    if (auvcan_init(&can, &cfg) != 0) bsp_fatal("auvcan_init"); //bus not connected properlly

#if BOARD_REQUIRE_HOST
    auvcan_fresh_init(&host_fresh, AUVCAN_HOST_HB_TIMEOUT_MS);
    if (auvcan_subscribe(&can, CUAUV_HOSTHEARTBEAT_ID, CUAUV_HOSTHEARTBEAT_SIGNATURE,
                         on_host_heartbeat, NULL) != 0) bsp_fatal("subscribe HostHeartbeat");
#endif

//     auvcan_set_status_code(&can, 1u);
//     statuslog_init(&can);
// #if BOARD_BENCH_THROTTLE
//     statuslog_set_extra(bench_status, NULL);
// #endif
    if (auvcan_subscribe(&can, CUAUV_KILLREQUEST_ID, CUAUV_KILLREQUEST_SIGNATURE,
                         on_kill_request, NULL) != 0) bsp_fatal("subscribe KillRequest");

    auvcan_set_status_code(&can, 1u);
    statuslog_init(&can);
    statuslog_set_extra(ctrl_status, NULL);
    if (auvcan_start(&can) != 0) bsp_fatal("auvcan_start");
    next_kill_us = auvcan_micros(&can);         // broadcast first Kill on the first loop right away
}

//runs every 0.5 sec
void app_loop(void)
{
    auvcan_poll(&can);   // RX (runs on_host_heartbeat), NodeStatus, TX
    statuslog_poll(&can);
    const uint64_t now = auvcan_micros(&can);
    bsp_heartbeat(now);

    poll_button(now);

#if BOARD_REQUIRE_HOST
    const bool host_ok   = auvcan_fresh_ok(&host_fresh, now); //host loss becomes a kill
    const bool soft_kill = host_soft_kill;
#else
    const bool host_ok   = true;
    const bool soft_kill = false;
#endif
    const uint8_t nk_src = node_kill_src(now);
    const bool armed = !hard_kill && !soft_kill && host_ok && nk_src == 0u;
    st_soft = soft_kill; st_host = host_ok; st_nk = nk_src;

#if BOARD_BENCH_THROTTLE
    if (!armed && last_armed) {
        for (uint8_t i = 0; i < BENCH_N; i++) bench_cmd[i] = 0;     /* a kill also zeroes the bench throttle */
    }
#endif

    /* 20 Hz, plus immediately on any change of `armed` */
    if (now >= next_kill_us || armed != last_armed) {
        publish_kill(armed, host_ok, soft_kill, nk_src);
        next_kill_us += (uint64_t)AUVCAN_KILL_PERIOD_MS * 1000u;
        if (now >= next_kill_us) {
            next_kill_us = now + (uint64_t)AUVCAN_KILL_PERIOD_MS * 1000u;   /* resync after a stall */
        }
    }

#if BOARD_BENCH_THROTTLE
    if (now >= next_bench_us) {
        next_bench_us = now + BENCH_PERIOD_US;
        publish_bench_cmd(last_armed);
    }
#endif

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
