/*
 * test_thruster.c - thruster board logic on the simulated bus.
 *
 * Nodes: controller (1, publishes Kill), host (2, publishes RawCommand and
 * listens to ThrusterStatus), Thruster A (10) and B (11) with fake PWM, and a
 * rogue node (30) that also sends RawCommand.
 *
 *   ./build/test_thruster
 */
#include <stdio.h>
#include <string.h>

#include "auvcan.h"
#include "auvcan_port_sim.h"
#include "thruster.h"

#include <uavcan.protocol.NodeStatus.h>
#include <uavcan.equipment.esc.RawCommand.h>
#include <cuauv.Kill.h>
#include <cuauv.ThrusterStatus.h>

static int g_fail, g_pass;
#define CHECK(cond) do { if (cond) { g_pass++; } else { g_fail++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ------------------------------------------------------------------------- */
/* Fake PWM                                                                   */
/* ------------------------------------------------------------------------- */

typedef struct {
    uint16_t us[THRUSTER_CHANNELS];
    uint32_t writes;
} fake_pwm_t;

static void fake_write(void *ctx, uint8_t ch, uint16_t us)
{
    fake_pwm_t *p = ctx;
    if (ch < THRUSTER_CHANNELS) p->us[ch] = us;
    p->writes++;
}

/* ------------------------------------------------------------------------- */
/* Fixture                                                                    */
/* ------------------------------------------------------------------------- */

typedef struct {
    sim_bus_t  bus;
    sim_port_t p_ctrl, p_host, p_a, p_b, p_rogue;
    auvcan_t   ctrl, host, a, b, rogue;
    thruster_t ta, tb;
    fake_pwm_t pwm_a, pwm_b;

    /* controller */
    bool     kill_on, kill_armed;
    uint8_t  kill_seq, kill_tid;
    uint64_t next_kill_us;

    /* host */
    bool     cmd_on;
    int16_t  cmd[2 * THRUSTER_CHANNELS];   /* Orion units, sent x32 */
    uint8_t  cmd_len;
    uint8_t  cmd_tid;
    uint64_t next_cmd_us;

    /* host observations of Thruster A */
    struct cuauv_ThrusterStatus st_a;
    int      st_a_count;
    struct uavcan_protocol_NodeStatus ns_a;
    int      ns_a_count;
} fx_t;

static fx_t F;

static void host_on_status(auvcan_t *c, CanardRxTransfer *t, void *u)
{
    (void)c; (void)u;
    struct cuauv_ThrusterStatus s;
    if (t->source_node_id == AUVCAN_NODE_BENCH && !cuauv_ThrusterStatus_decode(t, &s)) {
        F.st_a = s;
        F.st_a_count++;
    }
}

static void host_on_ns(auvcan_t *c, CanardRxTransfer *t, void *u)
{
    (void)c; (void)u;
    struct uavcan_protocol_NodeStatus ns;
    if (t->source_node_id == AUVCAN_NODE_BENCH && !uavcan_protocol_NodeStatus_decode(t, &ns)) {
        F.ns_a = ns;
        F.ns_a_count++;
    }
}

static void send_rawcmd(auvcan_t *from, uint8_t *tid, const int16_t *orion, uint8_t len)
{
    struct uavcan_equipment_esc_RawCommand m;
    memset(&m, 0, sizeof(m));
    m.cmd.len = len;
    for (uint8_t i = 0; i < len; i++) m.cmd.data[i] = (int16_t)(orion[i] * THRUSTER_RAW_SCALE);
    uint8_t buf[UAVCAN_EQUIPMENT_ESC_RAWCOMMAND_MAX_SIZE];
    const uint32_t n = uavcan_equipment_esc_RawCommand_encode(&m, buf);
    auvcan_broadcast(from, UAVCAN_EQUIPMENT_ESC_RAWCOMMAND_SIGNATURE, UAVCAN_EQUIPMENT_ESC_RAWCOMMAND_ID,
                     tid, AUVCAN_PRIO_ACTUATOR, buf, (uint16_t)n);
}

static void send_kill(void)
{
    struct cuauv_Kill k = { .armed = F.kill_armed, .soft_kill = !F.kill_armed, .host_ok = true,
                            .seq = F.kill_seq++ };
    uint8_t buf[CUAUV_KILL_MAX_SIZE];
    const uint32_t n = cuauv_Kill_encode(&k, buf);
    auvcan_broadcast(&F.ctrl, CUAUV_KILL_SIGNATURE, CUAUV_KILL_ID, &F.kill_tid, AUVCAN_PRIO_KILL,
                     buf, (uint16_t)n);
}

static void make_thruster(auvcan_t *can, sim_port_t *p, thruster_t *t, fake_pwm_t *pwm,
                          uint8_t node, uint8_t offset, int8_t pol0)
{
    auvcan_config_t c = { .node_id = node, .name = "edu.cornell.cuauv.thruster", .sw_major = 0,
                          .kill_timeout_ms = AUVCAN_KILL_TIMEOUT_MS,
                          .on_kill_change = thruster_kill_cb, .kill_user = t };
    c.port = sim_port_init(p, &F.bus, node);
    CHECK(auvcan_init(can, &c) == 0);
    const thruster_cfg_t tc = { .cmd_offset = offset, .polarity = { pol0, 1, 1, 1, 1, 1, 1, 1 },
                                .ramp_period_ms = 10, .ramp_inc = 5, .ramp_dec = 5,
                                .cmd_source_node = AUVCAN_NODE_HOST };
    const thruster_pwm_t pw = { .ctx = pwm, .write_us = fake_write };
    CHECK(thruster_init(t, can, &tc, &pw) == 0);
    CHECK(auvcan_start(can) == 0);
}

static void fx_setup(int8_t a_pol0)
{
    memset(&F, 0, sizeof(F));
    sim_bus_init(&F.bus);
    F.bus.now_us = 1000000u;

    auvcan_config_t c = { .name = "ctrl", .kill_timeout_ms = 0 };
    c.node_id = AUVCAN_NODE_CONTROLLER; c.port = sim_port_init(&F.p_ctrl, &F.bus, 1);
    CHECK(auvcan_init(&F.ctrl, &c) == 0);
    CHECK(auvcan_start(&F.ctrl) == 0);

    c.node_id = AUVCAN_NODE_HOST; c.name = "host"; c.port = sim_port_init(&F.p_host, &F.bus, 2);
    CHECK(auvcan_init(&F.host, &c) == 0);
    CHECK(auvcan_subscribe(&F.host, CUAUV_THRUSTERSTATUS_ID, CUAUV_THRUSTERSTATUS_SIGNATURE,
                           host_on_status, NULL) == 0);
    CHECK(auvcan_subscribe(&F.host, UAVCAN_PROTOCOL_NODESTATUS_ID, UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                           host_on_ns, NULL) == 0);
    CHECK(auvcan_start(&F.host) == 0);

    c.node_id = AUVCAN_NODE_SENSOR; c.name = "rogue"; c.port = sim_port_init(&F.p_rogue, &F.bus, 30);
    CHECK(auvcan_init(&F.rogue, &c) == 0);
    CHECK(auvcan_start(&F.rogue) == 0);

    make_thruster(&F.a, &F.p_a, &F.ta, &F.pwm_a, AUVCAN_NODE_BENCH, 0, a_pol0);
    /* A second instance at cmd[8..15], only to exercise cmd_offset: the sub has one board. */
    make_thruster(&F.b, &F.p_b, &F.tb, &F.pwm_b, 11, THRUSTER_CHANNELS, 1);

    F.cmd_len = 2 * THRUSTER_CHANNELS;
    F.next_kill_us = F.next_cmd_us = F.bus.now_us;
}

static void step_1ms(void)
{
    F.bus.now_us += 1000u;
    if (F.kill_on && F.bus.now_us >= F.next_kill_us) {
        F.next_kill_us += AUVCAN_KILL_PERIOD_MS * 1000u;
        send_kill();
    }
    if (F.cmd_on && F.bus.now_us >= F.next_cmd_us) {
        F.next_cmd_us += 10000u;                                  /* 100 Hz */
        send_rawcmd(&F.host, &F.cmd_tid, F.cmd, F.cmd_len);
    }
    auvcan_poll(&F.ctrl);
    auvcan_poll(&F.host);
    auvcan_poll(&F.rogue);
    auvcan_poll(&F.a); thruster_poll(&F.ta);
    auvcan_poll(&F.b); thruster_poll(&F.tb);
    /* health is the app's job (sensor/app.c): same rule here */
    auvcan_set_health(&F.a, thruster_warning(&F.ta) ? UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING : UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK);
    auvcan_set_health(&F.b, thruster_warning(&F.tb) ? UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING : UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK);
}

static void run_ms(uint32_t ms) { for (uint32_t i = 0; i < ms; i++) step_1ms(); }

static void arm(void)
{
    F.kill_on = true; F.kill_armed = true;
    for (int i = 0; i < 200 && auvcan_is_killed(&F.a); i++) step_1ms();
}

/* ------------------------------------------------------------------------- */
/* Pure helpers                                                               */
/* ------------------------------------------------------------------------- */

static void test_conversions(void)
{
    printf("conversions\n");
    CHECK(thruster_from_raw(0) == 0);
    CHECK(thruster_from_raw(8160) == 255);
    CHECK(thruster_from_raw(-8160) == -255);
    CHECK(thruster_from_raw(8191) == 255);           /* int14 max clamps */
    CHECK(thruster_from_raw(-8192) == -255);
    CHECK(thruster_from_raw(31) == 0);                /* truncates toward 0 */
    CHECK(thruster_from_raw(-31) == 0);
    CHECK(thruster_from_raw(32 * 100) == 100);

    /* Orion desToPWM, value -> pulse */
    CHECK(thruster_pulse_us(0) == 1500);
    CHECK(thruster_pulse_us(1) == 1526);             /* deadband edge forward */
    CHECK(thruster_pulse_us(-1) == 1472);            /* deadband edge reverse (Orion floors) */
    CHECK(thruster_pulse_us(255) == 1900);
    CHECK(thruster_pulse_us(-255) == 1100);
    CHECK(thruster_pulse_us(128) == 2 * (763 + (128 * 187) / 255));
    CHECK(thruster_pulse_us(300) == 1900);           /* clamped */
    CHECK(thruster_pulse_us(-300) == 1100);

    int mono = 1;                                     /* monotonic over the whole range */
    for (int v = -255; v < 255; v++) mono &= thruster_pulse_us((int16_t)v) <= thruster_pulse_us((int16_t)(v + 1));
    CHECK(mono);
}

static void test_ramp_step(void)
{
    printf("ramp step\n");
    CHECK(thruster_ramp_step(0, 100, 5, 7) == 5);          /* spin up: inc */
    CHECK(thruster_ramp_step(100, 0, 5, 7) == 93);         /* spin down: dec */
    CHECK(thruster_ramp_step(0, -100, 5, 7) == -5);        /* spin up reverse: inc */
    CHECK(thruster_ramp_step(-100, 0, 5, 7) == -93);       /* spin down reverse: dec */
    CHECK(thruster_ramp_step(98, 100, 5, 7) == 100);       /* no overshoot */
    CHECK(thruster_ramp_step(3, 0, 5, 7) == 0);
    CHECK(thruster_ramp_step(4, -100, 5, 7) == 0);         /* reversing stops at 0 first */
    CHECK(thruster_ramp_step(0, -100, 5, 7) == -5);        /* ...then spins up the other way */
    CHECK(thruster_ramp_step(50, 50, 5, 7) == 50);

    int16_t v = 0; int ticks = 0;
    while (v != 255 && ticks < 1000) { v = thruster_ramp_step(v, 255, 5, 5); ticks++; }
    CHECK(ticks == 51);                                    /* 0 -> 255 in 510 ms at 10 ms/5 */
}

/* ------------------------------------------------------------------------- */
/* Behaviour on the bus                                                       */
/* ------------------------------------------------------------------------- */

static void test_boot(void)
{
    printf("boot\n");
    fx_setup(1);
    CHECK(F.pwm_a.us[0] == 1500 && F.pwm_a.us[1] == 1500 && F.pwm_a.us[2] == 1500);
    F.cmd_on = true; F.cmd[0] = 255;                       /* commands but no Kill: stays killed */
    run_ms(1500);
    CHECK(auvcan_is_killed(&F.a));
    CHECK(F.pwm_a.us[0] == 1500);
    CHECK(F.st_a_count >= 25 && F.st_a_count <= 31);       /* 20 Hz over 1.5 s */
    CHECK(F.st_a.killed && F.st_a.applied[0] == 0);
    CHECK(!F.st_a.stale);                                  /* commands are fresh, just killed */
    CHECK(F.ns_a_count >= 1 && (F.ns_a.vendor_specific_status_code & 1u) == 1u);
    CHECK(F.ns_a.health == UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK);   /* killed is not a fault */
}

static void test_ramp_on_bus(void)
{
    printf("ramp up on the bus\n");
    fx_setup(1);
    F.cmd_on = true; F.cmd[0] = 255;
    arm();
    CHECK(!auvcan_is_killed(&F.a));
    run_ms(50);
    CHECK(F.ta.current[0] > 0 && F.ta.current[0] < 50);    /* ramping, not stepped */
    run_ms(600);
    CHECK(F.ta.current[0] == 255);
    CHECK(F.pwm_a.us[0] == 1900);
    CHECK(F.pwm_a.us[1] == 1500 && F.pwm_a.us[2] == 1500);
    run_ms(60);
    CHECK(F.st_a.applied[0] == 255 && !F.st_a.killed && !F.st_a.stale);
}

static void test_kill_is_immediate_and_rearm_ramps(void)
{
    printf("kill: neutral at once, re-arm ramps from 0 (Orion regression)\n");
    fx_setup(1);
    F.cmd_on = true; F.cmd[0] = 255;
    arm();
    run_ms(700);
    CHECK(F.pwm_a.us[0] == 1900);

    /* Explicit Kill(killed): neutral within the poll that receives it. */
    F.kill_armed = false;
    F.next_kill_us = F.bus.now_us;                         /* send on the next ms */
    int ms = 0;
    while (!auvcan_is_killed(&F.a) && ms < 100) { step_1ms(); ms++; }
    CHECK(ms <= 2);
    CHECK(F.pwm_a.us[0] == 1500);
    CHECK(F.ta.current[0] == 0);
    run_ms(60);
    CHECK(F.st_a.killed && F.st_a.applied[0] == 0);       /* readback no longer lies */

    /* Re-arm with the host still commanding 255: must ramp, not jump. */
    arm();
    run_ms(15);
    CHECK(F.ta.current[0] > 0 && F.ta.current[0] <= 10);
    CHECK(F.pwm_a.us[0] < 1600);
    run_ms(600);
    CHECK(F.ta.current[0] == 255);
}

static void test_kill_timeout(void)
{
    printf("kill: Kill stops -> neutral after the 250 ms watchdog\n");
    fx_setup(1);
    F.cmd_on = true; F.cmd[0] = 200;
    arm();
    run_ms(600);
    CHECK(F.pwm_a.us[0] > 1800);
    F.kill_on = false;
    int ms = 0;
    while (!auvcan_is_killed(&F.a) && ms < 1000) { step_1ms(); ms++; }
    CHECK(ms >= 190 && ms <= 260);                         /* 250 ms after the last Kill frame */
    CHECK(F.pwm_a.us[0] == 1500);
}

static void test_stale(void)
{
    printf("stale command: ramp down at dec, flag, WARNING\n");
    fx_setup(1);
    F.cmd_on = true; F.cmd[0] = 100;
    arm();
    run_ms(400);
    CHECK(F.ta.current[0] == 100);
    F.cmd_on = false;
    run_ms(190);
    CHECK(!F.ta.stale && F.ta.current[0] == 100);          /* still within 200 ms */
    run_ms(30);
    CHECK(F.ta.stale);
    CHECK(F.ta.current[0] < 100 && F.ta.current[0] > 80);  /* ramping down, not dropped */
    run_ms(300);
    CHECK(F.ta.current[0] == 0 && F.pwm_a.us[0] == 1500);
    CHECK(!auvcan_is_killed(&F.a));                        /* stale is not a kill */
    run_ms(1100);
    CHECK(F.st_a.stale && !F.st_a.killed);
    CHECK(F.ns_a.health == UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING);

    F.cmd_on = true; F.next_cmd_us = F.bus.now_us;         /* commands resume */
    run_ms(1100);
    CHECK(!F.ta.stale && F.ta.current[0] == 100);
    CHECK(F.ns_a.health == UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK);
}

static void test_polarity_and_slices(void)
{
    printf("polarity and cmd[] slices\n");
    fx_setup(-1);                                          /* Thruster A channel 0 reversed */
    F.cmd_on = true;
    F.cmd[0] = 100; F.cmd[1] = -50; F.cmd[2] = 0;
    F.cmd[7] = 77;                                         /* T8: the last channel works too */
    F.cmd[8] = 30;  F.cmd[9] = 40;  F.cmd[10] = -60;       /* second instance's slice */
    arm();
    run_ms(500);
    CHECK(F.ta.current[0] == -100);                        /* physical output reversed */
    CHECK(F.pwm_a.us[0] < 1500);
    CHECK(thruster_applied(&F.ta, 0) == 100);              /* reported in the command frame */
    CHECK(F.ta.current[1] == -50 && F.ta.current[2] == 0);
    CHECK(F.ta.current[7] == 77 && F.pwm_a.us[7] > 1500);
    CHECK(F.tb.current[0] == 30 && F.tb.current[1] == 40 && F.tb.current[2] == -60);
    run_ms(60);
    CHECK(F.st_a.applied[0] == 100 && F.st_a.applied[1] == -50);
    CHECK(F.st_a.applied[7] == 77);                        /* all 8 in the 3-frame ThrusterStatus */
}

static void test_rejects(void)
{
    printf("rejects: wrong source, short array\n");
    fx_setup(1);
    arm();
    uint8_t tid = 0;
    int16_t full[2 * THRUSTER_CHANNELS];
    for (int i = 0; i < 2 * THRUSTER_CHANNELS; i++) full[i] = 255;
    for (int i = 0; i < 50; i++) { send_rawcmd(&F.rogue, &tid, full, 2 * THRUSTER_CHANNELS); run_ms(10); }
    CHECK(F.ta.cmds_accepted == 0 && F.ta.cmds_ignored >= 40);
    CHECK(F.ta.current[0] == 0 && F.ta.stale);

    F.cmd_on = true; F.cmd[0] = 100; F.cmd_len = THRUSTER_CHANNELS;   /* covers A's slice only */
    run_ms(300);
    CHECK(F.ta.cmds_accepted > 0 && !F.ta.stale);
    CHECK(F.tb.cmds_accepted == 0 && F.tb.stale);          /* B needs cmd[8..15] */

    F.cmd_len = THRUSTER_CHANNELS - 1;                     /* 7 entries: too short even for A */
    const uint32_t acc = F.ta.cmds_accepted;
    run_ms(300);
    CHECK(F.ta.cmds_accepted == acc);
    CHECK(F.tb.current[0] == 0);
}

int main(void)
{
    test_conversions();
    test_ramp_step();
    test_boot();
    test_ramp_on_bus();
    test_kill_is_immediate_and_rearm_ramps();
    test_kill_timeout();
    test_stale();
    test_polarity_and_slices();
    test_rejects();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
