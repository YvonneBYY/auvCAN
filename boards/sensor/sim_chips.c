#include "sim_chips.h"

#include <math.h>
#include <string.h>

#define MS_ADDR 0x76u

/* Datasheet example coefficients C1..C6; word 0 gets a valid CRC in init. */
static const uint16_t PROM_02BA[7] = { 0x0000, 46372, 43981, 29059, 27842, 31553, 28165 };
static const uint16_t PROM_30BA[7] = { 0x0000, 34982, 36352, 20328, 22354, 26646, 26146 };

void sim_chips_init(sim_chips_t *c, const uint64_t *now_us, ms5837_model_t model, uint8_t mcp_addr)
{
    memset(c, 0, sizeof(*c));
    c->now_us = now_us;
    c->ms_present = c->mcp_present = c->adc_present = true;
    c->model = model;
    memcpy(c->prom, model == MS5837_30BA ? PROM_30BA : PROM_02BA, sizeof(c->prom));
    c->prom[0] = 0x00A5;                                   /* factory bits */
    c->prom[0] = (uint16_t)((ms5837_crc4(c->prom) << 12) | c->prom[0]);
    c->pressure_pa = 101325.0f;
    c->mcp_addr = mcp_addr;
    c->mcp_temp_c = 23.5f;
    c->adc_raw = sim_hull_raw_for_atm(1.0f);
}

void sim_chips_set_depth(sim_chips_t *c, float depth_m, float surface_pa, float water_density)
{
    c->pressure_pa = surface_pa + depth_m * water_density * 9.81f;
}

uint16_t sim_hull_raw_for_atm(float atm)
{
    const float ratio = atm * 101.325f * 0.01059f - 0.10941f;
    const float raw = ratio * 65535.0f + 0.5f;
    return raw <= 1.0f ? 1u : raw >= 65534.0f ? 65534u : (uint16_t)raw;
}

/* D2 = C5 * 256 makes dT = 0: TEMP is exactly 20.00 °C and no second-order terms
 * apply, so D1 can be solved for the wanted pressure from the first-order formula. */
static uint32_t d2_value(const sim_chips_t *c) { return (uint32_t)c->prom[5] * 256u; }

static uint32_t d1_value(const sim_chips_t *c)
{
    double sens, off, p, div;
    if (c->model == MS5837_30BA) {
        sens = (double)c->prom[1] * 32768.0; off = (double)c->prom[2] * 65536.0;
        p = c->pressure_pa / 10.0; div = 8192.0;            /* 0.1 mbar */
    } else {
        sens = (double)c->prom[1] * 65536.0; off = (double)c->prom[2] * 131072.0;
        p = c->pressure_pa; div = 32768.0;                  /* Pa */
    }
    return (uint32_t)ceil(((p * div + off) * 2097152.0) / sens);
}

static int sim_i2c_write(void *ctx, uint8_t addr, const uint8_t *data, uint8_t n)
{
    sim_chips_t *c = ctx;
    if (n == 0) return -1;
    if (addr == MS_ADDR) {
        if (!c->ms_present) return -1;
        const uint8_t cmd = data[0];
        if (cmd == 0x1E) {                                  /* reset */
            c->ms_pending = 0;
        } else if ((cmd & 0xF0) == 0x40 || (cmd & 0xF0) == 0x50) {   /* convert D1 / D2 */
            c->ms_pending = ((cmd & 0xF0) == 0x40) ? 1 : 2;
            c->ms_conv_start_us = *c->now_us;
            c->ms_result = 0;
        }
        c->ms_reg = cmd;
        return 0;
    }
    if (addr == c->mcp_addr) {
        if (!c->mcp_present) return -1;
        c->mcp_reg = data[0];
        return 0;
    }
    return -1;                                              /* nobody at that address: NACK */
}

static int sim_i2c_read(void *ctx, uint8_t addr, uint8_t *out, uint8_t n)
{
    sim_chips_t *c = ctx;
    memset(out, 0, n);
    if (addr == MS_ADDR) {
        if (!c->ms_present) return -1;
        if (c->ms_reg >= 0xA0 && c->ms_reg <= 0xAC && n >= 2) {
            const uint16_t w = c->prom[(c->ms_reg - 0xA0) / 2];
            out[0] = (uint8_t)(w >> 8); out[1] = (uint8_t)w;
        } else if (c->ms_reg == 0x00 && n >= 3) {
            uint32_t v = 0;
            if (c->ms_early_reads > 0u) {
                c->ms_early_reads--;                        /* forced "not finished" */
            } else if (c->ms_pending && *c->now_us - c->ms_conv_start_us >= 9040u) {
                v = (c->ms_pending == 1) ? d1_value(c) : d2_value(c);
            }                                               /* too early: reads 0 */
            c->ms_pending = 0;
            out[0] = (uint8_t)(v >> 16); out[1] = (uint8_t)(v >> 8); out[2] = (uint8_t)v;
        }
        return 0;
    }
    if (addr == c->mcp_addr) {
        if (!c->mcp_present || n < 2) return -1;
        if (c->mcp_reg == 0x05) {
            int32_t raw = (int32_t)lroundf(c->mcp_temp_c * 16.0f);
            uint16_t r13 = (uint16_t)(raw & 0x1FFF);
            out[0] = (uint8_t)((r13 >> 8) | 0xE0);              /* top 3 bits are alert flags */
            out[1] = (uint8_t)r13;
        } else if (c->mcp_reg == 0x06) {
            out[0] = 0x00; out[1] = 0x54;
        }
        return 0;
    }
    return -1;
}

static int sim_spi_read(void *ctx, uint8_t *rx, uint8_t n)
{
    sim_chips_t *c = ctx;
    const uint16_t v = c->adc_present ? c->adc_raw : 0xFFFFu;
    for (uint8_t i = 0; i < n; i++) rx[i] = (i == 0) ? 0xA5 : (i == 1) ? (uint8_t)(v >> 8) : (uint8_t)v;
    return 0;
}

static uint8_t sim_leak_raw(void *ctx) { return ((sim_chips_t *)ctx)->leaks; }

sensor_hw_t sim_chips_hw(sim_chips_t *c)
{
    return (sensor_hw_t){ .ctx = c, .i2c_write = sim_i2c_write, .i2c_read = sim_i2c_read,
                          .spi_read = sim_spi_read, .leak_raw = sim_leak_raw };
}
