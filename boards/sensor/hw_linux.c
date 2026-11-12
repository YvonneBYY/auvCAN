/*
 * hw_linux.c - the sensor board's chips simulated, for the mcast:0 virtual bus.
 *
 *   depth      slow sine between 0 and 2 m (30 s period), or fixed with
 *              AUVCAN_SENSOR_DEPTH=1.5
 *   leaks      the file $AUVCAN_LEAK_FILE (default /tmp/auvcan_leak): four 0/1
 *              characters for leak 1..4, e.g.  echo 0100 > /tmp/auvcan_leak
 *   internal temp 23.5 °C, hull pressure 1.00 atm
 */
#define _DEFAULT_SOURCE
#include "sensor_hw.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "board_config.h"
#include "sim_chips.h"

static sim_chips_t s_chips;
static uint64_t    s_now_us, s_t0_us;
static sensor_hw_t s_sim, s_hw;
static float       s_fixed_depth = NAN;

static uint64_t mono_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void tick(void)                     /* the chips see real time and a moving sub */
{
    s_now_us = mono_us();
    const float t = (float)(s_now_us - s_t0_us) / 1e6f;
    const float depth = isnan(s_fixed_depth) ? 1.0f - cosf(t * 2.0f * 3.14159265f / 30.0f) : s_fixed_depth;
    sim_chips_set_depth(&s_chips, depth, SENSOR_SURFACE_PA, SENSOR_WATER_DENSITY);
}

static int l_i2c_write(void *ctx, uint8_t a, const uint8_t *d, uint8_t n) { (void)ctx; tick(); return s_sim.i2c_write(s_sim.ctx, a, d, n); }
static int l_i2c_read(void *ctx, uint8_t a, uint8_t *d, uint8_t n)        { (void)ctx; tick(); return s_sim.i2c_read(s_sim.ctx, a, d, n); }
static int l_spi_read(void *ctx, uint8_t *rx, uint8_t n)                  { (void)ctx; return s_sim.spi_read(s_sim.ctx, rx, n); }

static uint8_t l_leak_raw(void *ctx)
{
    (void)ctx;
    const char *path = getenv("AUVCAN_LEAK_FILE");
    FILE *f = fopen(path ? path : "/tmp/auvcan_leak", "r");
    if (f == NULL) return 0;
    char buf[8] = {0};
    const size_t n = fread(buf, 1, 4, f);
    fclose(f);
    uint8_t bits = 0;
    for (size_t i = 0; i < n && i < SENSOR_LEAKS; i++) if (buf[i] == '1') bits |= (uint8_t)(1u << i);
    return bits;
}

const sensor_hw_t *sensor_hw_init(void)
{
    s_t0_us = s_now_us = mono_us();
    const char *d = getenv("AUVCAN_SENSOR_DEPTH");
    if (d != NULL) s_fixed_depth = strtof(d, NULL);
    sim_chips_init(&s_chips, &s_now_us, SENSOR_MS5837_MODEL, SENSOR_MCP9808_ADDR);
    tick();
    s_sim = sim_chips_hw(&s_chips);
    s_hw = (sensor_hw_t){ .ctx = NULL, .i2c_write = l_i2c_write, .i2c_read = l_i2c_read,
                          .spi_read = l_spi_read, .leak_raw = l_leak_raw };
    return &s_hw;
}
