/*
 * test_sensor.c - sensor board logic (boards/sensor) on the simulated bus, with
 * the chips modelled in boards/sensor/sim_chips.c.
 *
 * Nodes: the sensor board (30) and a host (2) that records Depth, HullStatus and
 * NodeStatus.
 *
 *   ./build/test_sensor
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "auvcan.h"
#include "auvcan_port_sim.h"
#include "sensor.h"
#include "sim_chips.h"

#include <uavcan.protocol.NodeStatus.h>
#include <cuauv.Depth.h>
#include <cuauv.HullStatus.h>

static int g_fail, g_pass;
#define CHECK(cond) do { if (cond) { g_pass++; } else { g_fail++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b, tol) (fabsf((float)(a) - (float)(b)) <= (tol))

static struct {
    sim_bus_t   bus;
    sim_port_t  p_host, p_sens;
    auvcan_t    host, sens;
    sim_chips_t chips;
    sensor_t    s;

    struct cuauv_Depth      depth;      uint32_t depth_count;
    struct cuauv_HullStatus hull;       uint32_t hull_count;
    uint8_t                 hull_prio;  uint64_t hull_at_us;
    struct uavcan_protocol_NodeStatus ns;
} F;

static void on_depth(auvcan_t *c, CanardRxTransfer *t, void *u)
{
    (void)c; (void)u;
    if (t->source_node_id == AUVCAN_NODE_SENSOR && !cuauv_Depth_decode(t, &F.depth)) F.depth_count++;
}

static void on_hull(auvcan_t *c, CanardRxTransfer *t, void *u)
{
    (void)c; (void)u;
    if (t->source_node_id == AUVCAN_NODE_SENSOR && !cuauv_HullStatus_decode(t, &F.hull)) {
        F.hull_count++;
        F.hull_prio = t->priority;
        F.hull_at_us = F.bus.now_us;
    }
}

static void on_ns(auvcan_t *c, CanardRxTransfer *t, void *u)
{
    (void)c; (void)u;
    if (t->source_node_id == AUVCAN_NODE_SENSOR) (void)uavcan_protocol_NodeStatus_decode(t, &F.ns);
}

static const sensor_cfg_t CFG_02 = { .model = MS5837_02BA, .water_density = 1000.0f,
                                     .surface_pa = 101325.0f, .mcp9808_addr = 0x18 };

static void fx_setup(sensor_cfg_t cfg)
{
    memset(&F, 0, sizeof(F));
    sim_bus_init(&F.bus);
    F.bus.now_us = 1000000u;

    auvcan_config_t c = { .name = "host", .node_id = AUVCAN_NODE_HOST, .kill_timeout_ms = 0 };
    c.port = sim_port_init(&F.p_host, &F.bus, 2);
    CHECK(auvcan_init(&F.host, &c) == 0);
    CHECK(auvcan_subscribe(&F.host, CUAUV_DEPTH_ID, CUAUV_DEPTH_SIGNATURE, on_depth, NULL) == 0);
    CHECK(auvcan_subscribe(&F.host, CUAUV_HULLSTATUS_ID, CUAUV_HULLSTATUS_SIGNATURE, on_hull, NULL) == 0);
    CHECK(auvcan_subscribe(&F.host, UAVCAN_PROTOCOL_NODESTATUS_ID, UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE, on_ns, NULL) == 0);
    CHECK(auvcan_start(&F.host) == 0);

    c.name = "sensor"; c.node_id = AUVCAN_NODE_SENSOR;
    c.port = sim_port_init(&F.p_sens, &F.bus, 30);
    CHECK(auvcan_init(&F.sens, &c) == 0);
    sim_chips_init(&F.chips, &F.bus.now_us, cfg.model, cfg.mcp9808_addr);
    const sensor_hw_t hw = sim_chips_hw(&F.chips);
    CHECK(sensor_init(&F.s, &F.sens, &cfg, &hw) == 0);
    CHECK(auvcan_start(&F.sens) == 0);
}

static void step_100us(void)
{
    F.bus.now_us += 100u;
    auvcan_poll(&F.sens); sensor_poll(&F.s);
    auvcan_set_health(&F.sens, sensor_warning(&F.s) ? UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING : UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK);   /* as app.c does */
    auvcan_poll(&F.host);
}

static void run_ms(uint32_t ms) { for (uint32_t i = 0; i < ms * 10u; i++) step_100us(); }

/* ------------------------------------------------------------------------- */

static void test_ms5837_math(void)
{
    printf("MS5837 maths (datasheet examples) and PROM CRC\n");
    int32_t t, p;
    const uint16_t c02[7] = { 0, 46372, 43981, 29059, 27842, 31553, 28165 };
    ms5837_compute(MS5837_02BA, c02, 6465444u, 8077636u, &t, &p);
    CHECK(t == 2000 && p == 110002);                       /* 20.00 °C, 1100.02 mbar */

    /* 30BA example is first-order (19.81 °C, 3999.8 mbar). At TEMP < 20 °C the
     * second-order terms then lower TEMP slightly and shift P a little. */
    const uint16_t c30[7] = { 0, 34982, 36352, 20328, 22354, 26646, 26146 };
    ms5837_compute(MS5837_30BA, c30, 4958179u, 6815414u, &t, &p);
    CHECK(t <= 1981 && t >= 1975);
    CHECK(p > 399000 && p < 400500);

    /* Orion-style input: 02BA formulas on a 30BA chip give a very wrong pressure. */
    int32_t t_wrong, p_wrong;
    ms5837_compute(MS5837_02BA, c30, 4958179u, 6815414u, &t_wrong, &p_wrong);
    CHECK(p_wrong < 300000 || p_wrong > 500000);

    sim_chips_t ch;
    uint64_t now = 0;
    sim_chips_init(&ch, &now, MS5837_02BA, 0x18);
    CHECK((ch.prom[0] >> 12) == ms5837_crc4(ch.prom));
    uint16_t bad[7];
    memcpy(bad, ch.prom, sizeof(bad));
    bad[3] ^= 0x0100u;                                     /* one flipped bit */
    CHECK((bad[0] >> 12) != ms5837_crc4(bad));
}

static void test_conversions(void)
{
    printf("MCP9808 and hull-pressure conversions\n");
    CHECK(NEAR(mcp9808_celsius(0x01, 0x94), 25.25f, 1e-4f));
    CHECK(NEAR(mcp9808_celsius(0xC1, 0x94), 25.25f, 1e-4f));  /* alert flag bits ignored */
    CHECK(NEAR(mcp9808_celsius(0x1F, 0xFC), -0.25f, 1e-4f));  /* Orion returned +0.25 here */
    CHECK(NEAR(mcp9808_celsius(0x1E, 0x80), -24.0f, 1e-4f));
    CHECK(NEAR(hull_atm_from_raw(sim_hull_raw_for_atm(1.0f)), 1.0f, 0.002f));
    CHECK(NEAR(hull_atm_from_raw(sim_hull_raw_for_atm(0.8f)), 0.8f, 0.002f));
}

static void test_rates_and_values(void)
{
    printf("Depth 50 Hz and HullStatus 10 Hz with the right values\n");
    fx_setup(CFG_02);
    sim_chips_set_depth(&F.chips, 1.25f, 101325.0f, 1000.0f);
    run_ms(100);                                           /* settle: reset + PROM + first cycles */
    const uint32_t d0 = F.depth_count, h0 = F.hull_count;
    run_ms(1000);
    CHECK(F.depth_count - d0 >= 49 && F.depth_count - d0 <= 51);
    CHECK(F.hull_count - h0 >= 9 && F.hull_count - h0 <= 11);
    CHECK(F.depth.sensor_ok);
    CHECK(NEAR(F.depth.depth_m, 1.25f, 0.002f));
    CHECK(NEAR(F.depth.water_temp_c, 20.0f, 0.05f));      /* float16 on the wire */
    CHECK(F.hull.pressure_sensor_ok && NEAR(F.hull.hull_pressure_atm, 1.0f, 0.002f));
    CHECK(NEAR(F.hull.internal_temp_c, 23.5f, 0.05f));
    CHECK(F.hull_prio == AUVCAN_PRIO_TELEM_SLOW);
    CHECK(F.ns.health == UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK && F.ns.vendor_specific_status_code == 0);
    CHECK(F.s.crc_errors == 0 && F.s.i2c_errors == 0);

    sim_chips_set_depth(&F.chips, 3.0f, 101325.0f, 1000.0f);   /* sub dives */
    run_ms(60);
    CHECK(NEAR(F.depth.depth_m, 3.0f, 0.002f));

    fx_setup((sensor_cfg_t){ .model = MS5837_30BA, .water_density = 1025.0f, .surface_pa = 101000.0f, .mcp9808_addr = 0x18 });
    sim_chips_set_depth(&F.chips, 12.0f, 101000.0f, 1025.0f);  /* 30BA: deeper than a 02BA reaches */
    run_ms(200);
    CHECK(F.depth.sensor_ok && NEAR(F.depth.depth_m, 12.0f, 0.01f));
}

static void test_leak_event(void)
{
    printf("leak: debounced, sent at once at the hull-event priority\n");
    fx_setup(CFG_02);
    run_ms(200);
    const uint32_t ev0 = F.s.leak_events;

    F.chips.leaks = 0x2;                                   /* 15 ms glitch: ignored */
    run_ms(15);
    F.chips.leaks = 0;
    run_ms(100);
    CHECK(F.s.leak_events == ev0 && !F.hull.leak[1]);

    F.chips.leaks = 0x2;                                   /* real leak on input 1 */
    const uint64_t t0 = F.bus.now_us;
    for (int i = 0; i < 1000 && !F.hull.leak[1]; i++) step_100us();
    CHECK(F.hull.leak[1] && !F.hull.leak[0] && !F.hull.leak[2] && !F.hull.leak[3]);
    CHECK(F.hull_prio == AUVCAN_PRIO_HULL_EVENT);
    CHECK(F.hull_at_us - t0 <= 40000u);                    /* within the 30 ms debounce + 10 ms */
    run_ms(1100);
    CHECK((F.ns.vendor_specific_status_code & SENSOR_CODE_LEAK_MASK) == (0x2u << SENSOR_CODE_LEAK_SHIFT));
    CHECK((F.ns.vendor_specific_status_code & 1u) == 0u);                /* bit 0 is not ours */
    CHECK(F.hull.leak[1] && F.hull_prio == AUVCAN_PRIO_TELEM_SLOW);   /* periodic keeps reporting it */

    F.chips.leaks = 0;                                     /* dries: also an event */
    for (int i = 0; i < 1000 && F.hull.leak[1]; i++) step_100us();
    CHECK(!F.hull.leak[1] && F.hull_prio == AUVCAN_PRIO_HULL_EVENT);
}

static void test_adc_early(void)
{
    printf("MS5837 read before the conversion finished: restart it, don't fail\n");
    fx_setup(CFG_02);
    run_ms(200);
    const uint32_t e0 = F.s.i2c_errors;
    F.chips.ms_early_reads = 1;                            /* one "not finished" answer */
    run_ms(200);
    CHECK(F.s.adc_early == 1u && F.s.i2c_errors == e0);
    CHECK(F.depth.sensor_ok && !isnan(F.depth.depth_m));
    CHECK(!(F.ns.vendor_specific_status_code & SENSOR_CODE_DEPTH_FAULT));
    F.chips.ms_early_reads = 3;                            /* three in a row: still recovers */
    run_ms(200);
    CHECK(F.s.adc_early == 4u && F.s.i2c_errors == e0 && F.depth.sensor_ok);
    F.chips.ms_early_reads = 4;                            /* four in a row: the chip is broken */
    run_ms(100);
    CHECK(F.s.i2c_errors == e0 + 1u && !F.depth.sensor_ok);
    run_ms(1300);                                          /* normal retry brings it back */
    CHECK(F.depth.sensor_ok && !isnan(F.depth.depth_m));
}

static void test_faults_and_recovery(void)
{
    printf("faults: NaN + flags + health WARNING, then recovery\n");
    fx_setup(CFG_02);
    run_ms(200);

    F.chips.ms_present = false;                            /* depth sensor unplugged */
    run_ms(100);
    CHECK(!F.depth.sensor_ok && isnan(F.depth.depth_m) && isnan(F.depth.water_temp_c));
    const uint32_t d0 = F.depth_count;
    run_ms(1000);
    CHECK(F.depth_count - d0 >= 48);                       /* still 50 Hz, as "not ok" */
    CHECK(F.ns.vendor_specific_status_code & SENSOR_CODE_DEPTH_FAULT);
    CHECK(F.ns.health == UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING);
    F.chips.ms_present = true;                             /* plugged back in: retried within ~1 s */
    run_ms(1300);
    CHECK(F.depth.sensor_ok && !isnan(F.depth.depth_m));

    F.chips.mcp_present = false;
    run_ms(300);
    CHECK(isnan(F.hull.internal_temp_c));
    run_ms(1100);
    CHECK(F.ns.vendor_specific_status_code & SENSOR_CODE_TEMP_FAULT);
    F.chips.mcp_present = true;
    run_ms(1300);
    CHECK(NEAR(F.hull.internal_temp_c, 23.5f, 0.05f));

    F.chips.adc_present = false;                           /* ADC output stuck at the rail */
    run_ms(300);
    CHECK(!F.hull.pressure_sensor_ok && isnan(F.hull.hull_pressure_atm));
    run_ms(1100);
    CHECK(F.ns.vendor_specific_status_code & SENSOR_CODE_HULL_FAULT);
    F.chips.adc_present = true;
    run_ms(1300);
    CHECK(F.hull.pressure_sensor_ok && F.ns.vendor_specific_status_code == 0);
    CHECK(F.ns.health == UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK);

    fx_setup(CFG_02);                                      /* corrupted PROM: refused, keeps retrying */
    F.chips.prom[2] ^= 0x0040u;
    run_ms(1500);
    CHECK(!F.depth.sensor_ok && F.s.crc_errors >= 1);
}

int main(void)
{
    test_ms5837_math();
    test_conversions();
    test_rates_and_values();
    test_leak_event();
    test_adc_early();
    test_faults_and_recovery();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
