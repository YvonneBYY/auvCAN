/*
 * test_core.c - host-side tests for the auvCAN core on a simulated bus.
 *
 *   cmake -S . -B build && cmake --build build && ./build/test_core
 */
#include <stdio.h>
#include <string.h>

#include "auvcan.h"
#include "auvcan_bittiming.h"
#include "auvcan_port_sim.h"
#include "can_ring.h"

#include <uavcan.protocol.NodeStatus.h>
#include <uavcan.protocol.GetNodeInfo.h>
#include <uavcan.protocol.GetNodeInfo_req.h>
#include <uavcan.protocol.GetNodeInfo_res.h>
#include <uavcan.protocol.RestartNode.h>
#include <uavcan.protocol.RestartNode_req.h>
#include <uavcan.protocol.RestartNode_res.h>
#include <cuauv.Kill.h>

static int g_fail, g_pass;
#define CHECK(cond) do { if (cond) { g_pass++; } else { g_fail++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ------------------------------------------------------------------------- */
/* Fixture: controller (1), host (2), thruster A (10), sensor (30)            */
/* ------------------------------------------------------------------------- */

typedef struct {
    sim_bus_t  bus;
    sim_port_t p_ctrl, p_host, p_thr, p_sen;
    auvcan_t   ctrl, host, thr, sen;

    /* kill publisher state (stands in for the controller board app) */
    bool     kill_publishing;
    bool     kill_armed;
    bool     kill_freeze_seq;
    uint8_t  kill_seq, kill_tid;
    uint64_t next_kill_us;

    /* observations */
    int      kill_edges, kill_last;
    int      ns_from_thr;
    struct uavcan_protocol_NodeStatus ns_last;
    bool     got_info;
    struct uavcan_protocol_GetNodeInfoResponse info;
    int      restart_rsp;            /* -1 none, 0 false, 1 true */
} fx_t;

static fx_t F;

static void on_kill_change(auvcan_t *can, bool killed, void *user)
{
    (void)can; (void)user;
    F.kill_edges++;
    F.kill_last = killed;
}

static void host_on_nodestatus(auvcan_t *can, CanardRxTransfer *t, void *user)
{
    (void)can; (void)user;
    if (t->source_node_id == AUVCAN_NODE_THRUSTER_A &&
        !uavcan_protocol_NodeStatus_decode(t, &F.ns_last)) {
        F.ns_from_thr++;
    }
}

static void host_on_info(auvcan_t *can, CanardRxTransfer *t, void *user)
{
    (void)can; (void)user;
    F.got_info = !uavcan_protocol_GetNodeInfoResponse_decode(t, &F.info);
}

static void host_on_restart(auvcan_t *can, CanardRxTransfer *t, void *user)
{
    (void)can; (void)user;
    struct uavcan_protocol_RestartNodeResponse r;
    if (!uavcan_protocol_RestartNodeResponse_decode(t, &r)) F.restart_rsp = r.ok ? 1 : 0;
}

static void publish_kill_from(auvcan_t *from)
{
    struct cuauv_Kill k = { .armed = F.kill_armed, .hard_kill = false,
                            .soft_kill = !F.kill_armed, .host_ok = true, .seq = F.kill_seq };
    if (!F.kill_freeze_seq) F.kill_seq++;
    uint8_t buf[CUAUV_KILL_MAX_SIZE];
    const uint32_t len = cuauv_Kill_encode(&k, buf);
    auvcan_broadcast(from, CUAUV_KILL_SIGNATURE, CUAUV_KILL_ID, &F.kill_tid,
                     AUVCAN_PRIO_KILL, buf, (uint16_t)len);
}

static void fx_setup(void)
{
    memset(&F, 0, sizeof(F));
    F.restart_rsp = -1;
    sim_bus_init(&F.bus);
    F.bus.now_us = 1000000u;   /* start at t = 1 s, not 0 */

    auvcan_config_t c = { .name = "edu.cornell.cuauv.controller", .hw_major = 1, .sw_major = 0,
                          .sw_minor = 1, .kill_timeout_ms = 0 };
    c.node_id = AUVCAN_NODE_CONTROLLER; c.port = sim_port_init(&F.p_ctrl, &F.bus, 0x10);
    CHECK(auvcan_init(&F.ctrl, &c) == 0);

    c.node_id = AUVCAN_NODE_HOST; c.name = "edu.cornell.cuauv.host";
    c.port = sim_port_init(&F.p_host, &F.bus, 0x20);
    CHECK(auvcan_init(&F.host, &c) == 0);
    CHECK(auvcan_subscribe(&F.host, UAVCAN_PROTOCOL_NODESTATUS_ID, UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                           host_on_nodestatus, NULL) == 0);
    CHECK(auvcan_on_response(&F.host, UAVCAN_PROTOCOL_GETNODEINFO_ID, UAVCAN_PROTOCOL_GETNODEINFO_SIGNATURE,
                             host_on_info, NULL) == 0);
    CHECK(auvcan_on_response(&F.host, UAVCAN_PROTOCOL_RESTARTNODE_ID, UAVCAN_PROTOCOL_RESTARTNODE_SIGNATURE,
                             host_on_restart, NULL) == 0);

    c.node_id = AUVCAN_NODE_THRUSTER_A; c.name = "edu.cornell.cuauv.thruster_a";
    c.hw_major = 2; c.hw_minor = 3; c.sw_major = 0; c.sw_minor = 7; c.sw_vcs_commit = 0xC0FFEEu;
    c.kill_timeout_ms = AUVCAN_KILL_TIMEOUT_MS; c.on_kill_change = on_kill_change;
    c.port = sim_port_init(&F.p_thr, &F.bus, 0x30);
    CHECK(auvcan_init(&F.thr, &c) == 0);

    auvcan_config_t s = { .node_id = AUVCAN_NODE_SENSOR, .name = "edu.cornell.cuauv.sensor",
                          .kill_timeout_ms = 0 };
    s.port = sim_port_init(&F.p_sen, &F.bus, 0x40);
    CHECK(auvcan_init(&F.sen, &s) == 0);

    CHECK(auvcan_start(&F.ctrl) == 0);
    CHECK(auvcan_start(&F.host) == 0);
    CHECK(auvcan_start(&F.thr) == 0);
    CHECK(auvcan_start(&F.sen) == 0);
    F.next_kill_us = F.bus.now_us;
}

/* Advance simulated time in 1 ms steps, polling every node. */
static void run_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        F.bus.now_us += 1000u;
        if (F.kill_publishing && F.bus.now_us >= F.next_kill_us) {
            F.next_kill_us += AUVCAN_KILL_PERIOD_MS * 1000u;
            publish_kill_from(&F.ctrl);
        }
        auvcan_poll(&F.ctrl);
        auvcan_poll(&F.host);
        auvcan_poll(&F.thr);
        auvcan_poll(&F.sen);
    }
}

/* Run until the thruster's kill state changes or the limit passes; returns elapsed ms. */
static uint32_t ms_until_kill_state(bool want_killed, uint32_t limit_ms)
{
    for (uint32_t ms = 1; ms <= limit_ms; ms++) {
        run_ms(1);
        if (auvcan_is_killed(&F.thr) == want_killed) return ms;
    }
    return UINT32_MAX;
}

/* ------------------------------------------------------------------------- */
/* Tests                                                                      */
/* ------------------------------------------------------------------------- */

static void test_ring(void)
{
    puts("ring buffer");
    static can_ring_t r;
    can_ring_init(&r);
    CanardCANFrame f = { .id = CANARD_CAN_FRAME_EFF, .data_len = 1 }, o;

    for (uint32_t i = 0; i < CAN_RING_CAPACITY; i++) { f.data[0] = (uint8_t)i; CHECK(can_ring_push(&r, &f)); }
    CHECK(!can_ring_push(&r, &f));               /* full -> drop, no hang */
    CHECK(r.dropped == 1);
    for (uint32_t i = 0; i < CAN_RING_CAPACITY; i++) { CHECK(can_ring_pop(&r, &o) && o.data[0] == (uint8_t)i); }
    CHECK(!can_ring_pop(&r, &o));

    /* index wrap across 2^32 */
    r.head = r.tail = UINT32_MAX - 3u;
    for (uint8_t i = 0; i < 10; i++) { f.data[0] = i; CHECK(can_ring_push(&r, &f)); }
    CHECK(can_ring_count(&r) == 10);
    for (uint8_t i = 0; i < 10; i++) { CHECK(can_ring_pop(&r, &o) && o.data[0] == i); }
}

static void test_fresh(void)
{
    puts("freshness monitor");
    auvcan_fresh_t f;
    auvcan_fresh_init(&f, 200);
    CHECK(!auvcan_fresh_ok(&f, 5000000u));                 /* never fed -> stale (safe default) */
    auvcan_fresh_feed(&f, 5000000u);
    CHECK(auvcan_fresh_ok(&f, 5200000u));                  /* exactly at the limit */
    CHECK(!auvcan_fresh_ok(&f, 5200001u));
    CHECK(f.expirations == 1);
    CHECK(auvcan_fresh_feed_seq(&f, 6000000u, 7));
    CHECK(!auvcan_fresh_feed_seq(&f, 6100000u, 7));        /* repeated seq does not feed */
    CHECK(!auvcan_fresh_ok(&f, 6200001u));
}

static void test_bit_timing(void)
{
    puts("bit timing");
    auvcan_bit_timing_t bt;
    CHECK(auvcan_compute_bit_timing(16000000u, 1000000u, 875u, &bt) == 0);
    CHECK(bt.prescaler == 1 && bt.seg1 == 13 && bt.seg2 == 2);
    CHECK(auvcan_compute_bit_timing(24000000u, 1000000u, 875u, &bt) == 0);
    CHECK(bt.prescaler == 1 && bt.seg1 == 20 && bt.seg2 == 3);
    CHECK(auvcan_compute_bit_timing(80000000u, 1000000u, 875u, &bt) == 0);
    CHECK(bt.prescaler == 1 && bt.seg1 == 69 && bt.seg2 == 10);
    CHECK(auvcan_compute_bit_timing(170000000u, 1000000u, 875u, &bt) == 0);
    CHECK(bt.prescaler == 5 && bt.seg1 == 29 && bt.seg2 == 4);
    CHECK(auvcan_compute_bit_timing(85000000u, 1000000u, 875u, &bt) == 0);    /* Deft PCB: PCLK1 */
    CHECK(bt.prescaler == 5 && bt.seg1 == 14 && bt.seg2 == 2);               /* = Deft's own CubeMX timing */
    CHECK(auvcan_compute_bit_timing(170000000u, 500000u, 875u, &bt) == 0);
    CHECK(auvcan_compute_bit_timing(7372800u, 1000000u, 875u, &bt) != 0);   /* no exact timing */
}

static void test_filters(void)
{
    puts("hardware filters");
    fx_setup();
    /* thruster: [0] services to node 10, [1] Kill (high priority) */
    CHECK(F.p_thr.n_filters == 2);
    CHECK(F.p_thr.filters[0].id == ((10u << 8) | 0x80u) && F.p_thr.filters[0].mask == 0x7F80u);
    CHECK(F.p_thr.filters[1].id == (CUAUV_KILL_ID << 8) && F.p_thr.filters[1].high_priority);
    /* sensor (no kill): only its service filter */
    CHECK(F.p_sen.n_filters == 1);

    /* NodeStatus traffic must not reach the thruster's "hardware" at all */
    run_ms(3000);
    CHECK(F.p_thr.frames_rx == 0);
    CHECK(F.p_host.frames_rx > 0);
}

static void test_node_status(void)
{
    puts("NodeStatus");
    fx_setup();
    run_ms(2500);
    CHECK(F.ns_from_thr == 3);                             /* t = 0, 1, 2 s after boot */
    CHECK(F.ns_last.health == UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK);
    CHECK(F.ns_last.mode == UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL);
    CHECK(F.ns_last.uptime_sec == 2);

    auvcan_set_status_code(&F.thr, 0xBEEF);
    auvcan_set_health(&F.thr, UAVCAN_PROTOCOL_NODESTATUS_HEALTH_ERROR);
    run_ms(1000);
    CHECK(F.ns_last.vendor_specific_status_code == 0xBEEF);
    CHECK(F.ns_last.health == UAVCAN_PROTOCOL_NODESTATUS_HEALTH_ERROR);

    /* a bus-off raises health to WARNING for one status period */
    auvcan_set_health(&F.thr, UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK);
    F.p_thr.inject.bus_off_count = 1;
    run_ms(1000);
    CHECK(F.ns_last.health == UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING);
    run_ms(1000);
    CHECK(F.ns_last.health == UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK);
}

static void request_info(void)
{
    static uint8_t tid;
    auvcan_request(&F.host, AUVCAN_NODE_THRUSTER_A, UAVCAN_PROTOCOL_GETNODEINFO_SIGNATURE,
                   UAVCAN_PROTOCOL_GETNODEINFO_ID, &tid, AUVCAN_PRIO_SERVICE, NULL, 0);
}

static void test_get_node_info(void)
{
    puts("GetNodeInfo (multi-frame response)");
    fx_setup();
    request_info();
    run_ms(20);
    CHECK(F.got_info);
    CHECK(F.info.name.len == strlen("edu.cornell.cuauv.thruster_a"));
    CHECK(memcmp(F.info.name.data, "edu.cornell.cuauv.thruster_a", F.info.name.len) == 0);
    CHECK(F.info.software_version.minor == 7 && F.info.software_version.vcs_commit == 0xC0FFEEu);
    CHECK(F.info.hardware_version.major == 2 && F.info.hardware_version.minor == 3);
    CHECK(memcmp(F.info.hardware_version.unique_id, F.p_thr.uid, 16) == 0);

    /* hardware TX full for 30 ms: response must still arrive intact afterwards */
    memset(&F.info, 0, sizeof(F.info));
    F.got_info = false;
    F.p_thr.tx_blocked = true;
    request_info();
    run_ms(30);
    CHECK(!F.got_info);
    F.p_thr.tx_blocked = false;
    run_ms(5);
    CHECK(F.got_info && F.info.name.len == strlen("edu.cornell.cuauv.thruster_a"));
    printf("  thruster canard pool peak: %u blocks\n", auvcan_pool_peak(&F.thr));
}

static void test_restart(void)
{
    puts("RestartNode");
    fx_setup();
    static uint8_t tid;
    uint8_t buf[UAVCAN_PROTOCOL_RESTARTNODE_REQUEST_MAX_SIZE];
    struct uavcan_protocol_RestartNodeRequest req = { .magic_number = 1234 };
    uint32_t len = uavcan_protocol_RestartNodeRequest_encode(&req, buf);
    auvcan_request(&F.host, AUVCAN_NODE_THRUSTER_A, UAVCAN_PROTOCOL_RESTARTNODE_SIGNATURE,
                   UAVCAN_PROTOCOL_RESTARTNODE_ID, &tid, AUVCAN_PRIO_SERVICE, buf, (uint16_t)len);
    run_ms(200);
    CHECK(F.restart_rsp == 0 && F.p_thr.reboots == 0);    /* wrong magic: refused */

    req.magic_number = UAVCAN_PROTOCOL_RESTARTNODE_REQUEST_MAGIC_NUMBER;
    len = uavcan_protocol_RestartNodeRequest_encode(&req, buf);
    F.restart_rsp = -1;
    auvcan_request(&F.host, AUVCAN_NODE_THRUSTER_A, UAVCAN_PROTOCOL_RESTARTNODE_SIGNATURE,
                   UAVCAN_PROTOCOL_RESTARTNODE_ID, &tid, AUVCAN_PRIO_SERVICE, buf, (uint16_t)len);
    run_ms(50);
    CHECK(F.restart_rsp == 1 && F.p_thr.reboots == 0);    /* responded, reboot still deferred */
    run_ms(100);
    CHECK(F.p_thr.reboots == 1);
}

static void test_kill(void)
{
    puts("kill watchdog");
    fx_setup();

    CHECK(auvcan_is_killed(&F.thr));                       /* boots killed */
    CHECK(!auvcan_is_killed(&F.sen));                      /* non-actuator: n/a */
    run_ms(500);
    CHECK(auvcan_is_killed(&F.thr) && F.kill_edges == 0);  /* silence keeps it killed */

    /* controller publishes ARMED */
    F.kill_publishing = true; F.kill_armed = true; F.next_kill_us = F.bus.now_us;
    uint32_t t = ms_until_kill_state(false, 100);
    CHECK(t <= 2);
    CHECK(F.kill_edges == 1 && F.kill_last == 0);
    run_ms(1000);
    CHECK(!auvcan_is_killed(&F.thr) && F.kill_edges == 1); /* stays armed at 20 Hz */

    /* controller goes silent -> killed by timeout (250 ms after the last message) */
    F.kill_publishing = false;
    t = ms_until_kill_state(true, 1000);
    printf("  silence -> killed after %u ms\n", t);
    CHECK(t >= 200 && t <= 252);
    CHECK(F.kill_edges == 2 && F.kill_last == 1);

    /* re-arm, then explicit KILLED message acts on the same poll */
    F.kill_publishing = true; F.kill_armed = true; F.next_kill_us = F.bus.now_us;
    CHECK(ms_until_kill_state(false, 100) <= 2);
    run_ms(20);
    F.kill_armed = false; F.next_kill_us = F.bus.now_us;
    t = ms_until_kill_state(true, 100);
    CHECK(t <= 2);

    /* stuck publisher (same seq repeated) -> treated as silence */
    F.kill_armed = true; F.kill_freeze_seq = false; F.next_kill_us = F.bus.now_us;
    CHECK(ms_until_kill_state(false, 100) <= 2);
    F.kill_freeze_seq = true;
    t = ms_until_kill_state(true, 1000);
    printf("  stuck seq -> killed after %u ms\n", t);
    /* the first frozen frame still carries a new seq, so the 250 ms window
     * starts up to one 50 ms Kill period after the freeze */
    CHECK(t >= 250 && t <= 302);

    /* ARMED from the wrong node (the host) must be ignored */
    F.kill_publishing = false; F.kill_freeze_seq = false;
    run_ms(500);
    F.kill_armed = true;
    for (int i = 0; i < 20; i++) { publish_kill_from(&F.host); run_ms(50); }
    CHECK(auvcan_is_killed(&F.thr));
}

static void test_kill_no_glitch_moving_clock(void)
{
    puts("kill: no glitches when the clock moves mid-poll (regression)");
    fx_setup();
    F.bus.advance_per_read_us = 37;          /* every micros() read advances the clock */
    F.kill_publishing = true; F.kill_armed = true; F.next_kill_us = F.bus.now_us;
    CHECK(ms_until_kill_state(false, 100) != UINT32_MAX);
    const int edges = F.kill_edges;
    run_ms(3000);
    CHECK(!auvcan_is_killed(&F.thr));
    CHECK(F.kill_edges == edges);            /* armed the whole time: zero spurious kill edges */
    printf("  spurious edges over 3 s: %d\n", F.kill_edges - edges);
    F.bus.advance_per_read_us = 0;
}

static void test_fresh_clock_behind(void)
{
    puts("freshness: caller clock slightly behind the feed");
    auvcan_fresh_t f;
    auvcan_fresh_init(&f, 250);
    auvcan_fresh_feed(&f, 10001000u);
    CHECK(auvcan_fresh_ok(&f, 10000000u));   /* 1 ms "in the past" = just fed, not stale */
}

int main(void)
{
    test_ring();
    test_fresh();
    test_bit_timing();
    test_filters();
    test_node_status();
    test_get_node_info();
    test_restart();
    test_kill();
    test_kill_no_glitch_moving_clock();
    test_fresh_clock_behind();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
