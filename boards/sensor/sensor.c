#include "sensor.h"

#include <math.h>
#include <string.h>

#include <cuauv.Depth.h>
#include <cuauv.HullStatus.h>
#include <uavcan.protocol.NodeStatus.h>

/* ---- MS5837 ---------------------------------------------------------------- */
#define MS5837_ADDR          0x76u
#define MS5837_RESET         0x1Eu
#define MS5837_PROM_READ     0xA0u      /* + 2 * word */
#define MS5837_CONV_D1_4096  0x48u
#define MS5837_CONV_D2_4096  0x58u
#define MS5837_ADC_READ      0x00u
#define MS5837_RESET_US      10000u     /* reload of the PROM after reset */
#define MS5837_CONV_US       9500u      /* OSR 4096: 9.04 ms max */
#define MS5837_EARLY_MAX     3u         /* "not finished yet" answers in a row before failing */

/* ---- MCP9808 --------------------------------------------------------------- */
#define MCP9808_REG_TA       0x05u
#define MCP9808_REG_MANUF    0x06u
#define MCP9808_MANUF_ID     0x0054u

/* a / b rounded toward minus infinity (b > 0): the datasheets' integer maths
 * assume arithmetic shifts, which C leaves implementation-defined for negatives. */
static int64_t fdiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b) != 0 && a < 0) q--;
    return q;
}

uint8_t ms5837_crc4(const uint16_t prom[7])
{
    uint16_t n[8];
    memcpy(n, prom, 7 * sizeof(uint16_t));
    n[0] &= 0x0FFFu;                      /* the CRC itself is replaced by 0 */
    n[7] = 0;
    uint16_t rem = 0;
    for (int cnt = 0; cnt < 16; cnt++) {
        rem ^= (cnt & 1) ? (uint16_t)(n[cnt >> 1] & 0x00FFu) : (uint16_t)(n[cnt >> 1] >> 8u);
        for (int bit = 8; bit > 0; bit--) {
            rem = (uint16_t)(((uint32_t)rem << 1) ^ ((rem & 0x8000u) ? 0x3000u : 0u));
        }
    }
    return (uint8_t)((rem >> 12) & 0x0Fu);
}

void ms5837_compute(ms5837_model_t model, const uint16_t prom[7], uint32_t d1, uint32_t d2,
                    int32_t *temp_centi_c, int32_t *pressure_pa)
{
    const int64_t C1 = prom[1], C2 = prom[2], C3 = prom[3], C4 = prom[4], C5 = prom[5], C6 = prom[6];
    const int64_t dT   = (int64_t)d2 - C5 * 256;
    const int64_t TEMP = 2000 + fdiv(dT * C6, 8388608);              /* 0.01 °C */
    const int64_t t20  = TEMP - 2000;

    int64_t OFF, SENS, Ti = 0, OFFi = 0, SENSi = 0;
    if (model == MS5837_30BA) {
        OFF  = C2 * 65536  + fdiv(C4 * dT, 128);
        SENS = C1 * 32768  + fdiv(C3 * dT, 256);
        if (TEMP < 2000) {
            Ti    = fdiv(3 * dT * dT, 8589934592LL);                    /* 2^33 */
            OFFi  = fdiv(3 * t20 * t20, 2);
            SENSi = fdiv(5 * t20 * t20, 8);
            if (TEMP < -1500) {
                const int64_t t15 = TEMP + 1500;
                OFFi  += 7 * t15 * t15;
                SENSi += 4 * t15 * t15;
            }
        } else {
            Ti   = fdiv(2 * dT * dT, 137438953472LL);                   /* 2^37 */
            OFFi = fdiv(t20 * t20, 16);
        }
        const int64_t P = fdiv(fdiv((int64_t)d1 * (SENS - SENSi), 2097152) - (OFF - OFFi), 8192);
        *pressure_pa = (int32_t)(P * 10);                               /* 0.1 mbar -> Pa */
    } else {                                                            /* 02BA (Orion's formulas) */
        OFF  = C2 * 131072 + fdiv(C4 * dT, 64);
        SENS = C1 * 65536  + fdiv(C3 * dT, 128);
        if (TEMP < 2000) {
            Ti    = fdiv(11 * dT * dT, 34359738368LL);                  /* 2^35 */
            OFFi  = fdiv(31 * t20 * t20, 8);
            SENSi = fdiv(63 * t20 * t20, 32);
        }
        const int64_t P = fdiv(fdiv((int64_t)d1 * (SENS - SENSi), 2097152) - (OFF - OFFi), 32768);
        *pressure_pa = (int32_t)P;                                      /* 0.01 mbar = Pa */
    }
    *temp_centi_c = (int32_t)(TEMP - Ti);
}

float mcp9808_celsius(uint8_t upper, uint8_t lower)
{
    int32_t raw = ((int32_t)(upper & 0x1Fu) << 8) | lower;              /* 13-bit two's complement */
    if (raw & 0x1000) raw -= 0x2000;
    return (float)raw / 16.0f;
}

float hull_atm_from_raw(uint16_t raw)
{
    const float ratio = (float)raw / 65535.0f;                          /* Vout / Vsupply (ratiometric) */
    const float kpa = (ratio + 0.10941f) / 0.01059f;                    /* sensor datasheet, as in Orion */
    return kpa / 101.325f;
}

/* ---- I/O helpers --------------------------------------------------------------- */

static bool i2c_cmd(sensor_t *s, uint8_t addr, uint8_t cmd)
{
    return s->hw.i2c_write(s->hw.ctx, addr, &cmd, 1) == 0;
}

static bool i2c_reg_read(sensor_t *s, uint8_t addr, uint8_t reg, uint8_t *out, uint8_t n)
{
    return i2c_cmd(s, addr, reg) && s->hw.i2c_read(s->hw.ctx, addr, out, n) == 0;
}

/* 1 = result in *out, 0 = conversion not finished (the chip answers 0 and drops the
 * conversion), -1 = I2C error. */
static int ms_read_adc(sensor_t *s, uint32_t *out)
{
    uint8_t b[3];
    if (!i2c_reg_read(s, MS5837_ADDR, MS5837_ADC_READ, b, 3)) return -1;
    *out = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    return (*out != 0u) ? 1 : 0;
}


/* ---- publishing ------------------------------------------------------------------ */

static void publish_depth(sensor_t *s)
{
    struct cuauv_Depth m = {
        .depth_m      = s->depth_ok ? s->depth_m : NAN,
        .water_temp_c = s->depth_ok ? s->water_temp_c : NAN,
        .sensor_ok    = s->depth_ok,
    };
    uint8_t buf[CUAUV_DEPTH_MAX_SIZE];
    const uint32_t len = cuauv_Depth_encode(&m, buf);
    auvcan_broadcast(s->can, CUAUV_DEPTH_SIGNATURE, CUAUV_DEPTH_ID, &s->depth_tid,
                     AUVCAN_PRIO_SENSOR_FAST, buf, (uint16_t)len);
    s->depth_published++;
}

static void publish_hull(sensor_t *s, uint8_t priority)
{
    struct cuauv_HullStatus m = {
        .hull_pressure_atm  = s->hull_ok ? s->hull_atm : NAN,
        .internal_temp_c    = s->temp_ok ? s->internal_temp_c : NAN,
        .pressure_sensor_ok = s->hull_ok,
    };
    for (unsigned i = 0; i < SENSOR_LEAKS; i++) m.leak[i] = (((unsigned)s->leak_stable >> i) & 1u) != 0u;
    uint8_t buf[CUAUV_HULLSTATUS_MAX_SIZE];
    const uint32_t len = cuauv_HullStatus_encode(&m, buf);
    auvcan_broadcast(s->can, CUAUV_HULLSTATUS_SIGNATURE, CUAUV_HULLSTATUS_ID, &s->hull_tid,
                     priority, buf, (uint16_t)len);
    s->hull_published++;
}

/* ---- the three activities ------------------------------------------------------------ */

static void ms_fail(sensor_t *s, uint64_t now)
{
    s->depth_ok = false;
    s->i2c_errors++;
    s->ms_phase = MS_FAILED;
    s->ms_due_us = now + SENSOR_RETRY_US;
}

/* Keeps the 50 Hz Depth slots running even with no measurement (NaN, sensor_ok 0). */
static void depth_slot_without_measurement(sensor_t *s, uint64_t now)
{
    if (now >= s->next_depth_us) {
        s->next_depth_us += SENSOR_DEPTH_PERIOD_US;
        if (now >= s->next_depth_us) s->next_depth_us = now + SENSOR_DEPTH_PERIOD_US;
        publish_depth(s);
    }
}

/* Read a finished conversion. If it wasn't finished, start the same conversion again
 * (up to MS5837_EARLY_MAX times in a row) instead of declaring the sensor dead.
 * Returns true when *out holds a result; false = keep waiting or failed (ms_phase says). */
static bool ms_collect(sensor_t *s, uint64_t now, uint8_t conv_cmd, uint32_t *out)
{
    const int r = ms_read_adc(s, out);
    if (r > 0) { s->ms_early_run = 0; return true; }
    if (r == 0 && s->ms_early_run < MS5837_EARLY_MAX && i2c_cmd(s, MS5837_ADDR, conv_cmd)) {
        s->ms_early_run++;
        s->adc_early++;
        s->ms_due_us = now + MS5837_CONV_US;
        return false;
    }
    s->ms_early_run = 0;
    ms_fail(s, now);
    publish_depth(s);
    return false;
}

static void poll_depth(sensor_t *s, uint64_t now)
{
    switch (s->ms_phase) {
    case MS_FAILED:
        depth_slot_without_measurement(s, now);
        if (now >= s->ms_due_us) s->ms_phase = MS_RESET;
        break;

    case MS_RESET:
        depth_slot_without_measurement(s, now);
        if (!i2c_cmd(s, MS5837_ADDR, MS5837_RESET)) { ms_fail(s, now); break; }
        s->ms_phase = MS_RESET_WAIT;
        s->ms_due_us = now + MS5837_RESET_US;
        break;

    case MS_RESET_WAIT:
        depth_slot_without_measurement(s, now);
        if (now < s->ms_due_us) break;
        for (uint8_t i = 0; i < 7; i++) {
            uint8_t b[2];
            if (!i2c_reg_read(s, MS5837_ADDR, (uint8_t)(MS5837_PROM_READ + 2u * i), b, 2)) { ms_fail(s, now); return; }
            s->prom[i] = (uint16_t)((b[0] << 8) | b[1]);
        }
        if ((s->prom[0] >> 12) != ms5837_crc4(s->prom)) {
            s->crc_errors++;
            ms_fail(s, now);
            break;
        }
        s->ms_phase = MS_IDLE;
        break;

    case MS_IDLE:
        if (now < s->next_depth_us) break;
        s->next_depth_us += SENSOR_DEPTH_PERIOD_US;
        if (now >= s->next_depth_us) s->next_depth_us = now + SENSOR_DEPTH_PERIOD_US;   /* resync */
        if (!i2c_cmd(s, MS5837_ADDR, MS5837_CONV_D1_4096)) { ms_fail(s, now); publish_depth(s); break; }
        s->ms_phase = MS_WAIT_D1;
        s->ms_due_us = now + MS5837_CONV_US;
        break;

    case MS_WAIT_D1:
        if (now < s->ms_due_us) break;
        if (!ms_collect(s, now, MS5837_CONV_D1_4096, &s->d1)) break;
        if (!i2c_cmd(s, MS5837_ADDR, MS5837_CONV_D2_4096)) { ms_fail(s, now); publish_depth(s); break; }
        s->ms_phase = MS_WAIT_D2;
        s->ms_due_us = now + MS5837_CONV_US;
        break;

    case MS_WAIT_D2: {
        if (now < s->ms_due_us) break;
        uint32_t d2;
        if (!ms_collect(s, now, MS5837_CONV_D2_4096, &d2)) break;
        int32_t t_centi, p_pa;
        ms5837_compute(s->cfg.model, s->prom, s->d1, d2, &t_centi, &p_pa);
        s->water_temp_c = (float)t_centi / 100.0f;
        s->depth_m = ((float)p_pa - s->cfg.surface_pa) / (s->cfg.water_density * 9.81f);
        s->depth_ok = true;
        publish_depth(s);
        s->ms_phase = MS_IDLE;
        break;
    }
    }
}

static void poll_slow(sensor_t *s, uint64_t now)
{
    if (now < s->next_slow_us) return;
    s->next_slow_us += SENSOR_SLOW_PERIOD_US;
    if (now >= s->next_slow_us) s->next_slow_us = now + SENSOR_SLOW_PERIOD_US;

    /* internal temperature (MCP9808): check the manufacturer ID once per (re)connect */
    if (s->temp_ok || now >= s->temp_retry_us) {
        uint8_t b[2];
        bool ok = true;
        if (!s->temp_id_checked) {
            ok = i2c_reg_read(s, s->cfg.mcp9808_addr, MCP9808_REG_MANUF, b, 2) &&
                 (((uint16_t)b[0] << 8) | b[1]) == MCP9808_MANUF_ID;
            s->temp_id_checked = ok;
        }
        if (ok) ok = i2c_reg_read(s, s->cfg.mcp9808_addr, MCP9808_REG_TA, b, 2);
        if (ok) {
            s->internal_temp_c = mcp9808_celsius(b[0], b[1]);
        } else {
            s->i2c_errors++;
            s->temp_id_checked = false;
            s->temp_retry_us = now + SENSOR_RETRY_US;
        }
        s->temp_ok = ok;
        s->temp_fault = !ok;
    }

    /* hull pressure (external ADC): a reading pinned at either rail means no sensor */
    uint8_t r[3] = { 0, 0, 0 };
    const bool spi_ok = s->hw.spi_read(s->hw.ctx, r, 3) == 0;
    s->hull_raw = (uint16_t)((r[1] << 8) | r[2]);                   /* r[0] is junk, as in Orion */
    s->hull_ok = spi_ok && s->hull_raw != 0x0000u && s->hull_raw != 0xFFFFu;
    if (s->hull_ok) s->hull_atm = hull_atm_from_raw(s->hull_raw);

    publish_hull(s, AUVCAN_PRIO_TELEM_SLOW);
}

static void poll_leaks(sensor_t *s, uint64_t now)
{
    if (now < s->next_leak_us) return;
    s->next_leak_us += SENSOR_LEAK_SAMPLE_US;
    if (now >= s->next_leak_us) s->next_leak_us = now + SENSOR_LEAK_SAMPLE_US;

    const uint8_t raw = (uint8_t)(s->hw.leak_raw(s->hw.ctx) & 0x0Fu);
    if (raw == s->leak_candidate) {
        if (s->leak_count < 255u) s->leak_count++;
    } else {
        s->leak_candidate = raw;
        s->leak_count = 1;
    }
    if (s->leak_count >= SENSOR_LEAK_DEBOUNCE && s->leak_candidate != s->leak_stable) {
        s->leak_stable = s->leak_candidate;
        s->leak_events++;
        publish_hull(s, AUVCAN_PRIO_HULL_EVENT);                    /* now, ahead of telemetry */
    }
}

/* ---- public --------------------------------------------------------------------------- */

int sensor_init(sensor_t *s, auvcan_t *can, const sensor_cfg_t *cfg, const sensor_hw_t *hw)
{
    if (s == NULL || can == NULL || cfg == NULL || hw == NULL ||
        hw->i2c_write == NULL || hw->i2c_read == NULL || hw->spi_read == NULL || hw->leak_raw == NULL) {
        return -1;
    }
    memset(s, 0, sizeof(*s));
    s->can = can;
    s->cfg = *cfg;
    s->hw = *hw;
    const uint64_t now = auvcan_micros(can);
    s->ms_phase = MS_RESET;
    s->ms_due_us = now;
    s->next_depth_us = now;
    s->next_slow_us = now;
    s->next_leak_us = now;
    s->temp_retry_us = now;
    s->depth_m = s->water_temp_c = s->internal_temp_c = s->hull_atm = NAN;
    return 0;
}

void sensor_poll(sensor_t *s)
{
    /* A fresh clock reading, not can->poll_now_us: that one was taken before auvcan_poll()
     * handled this loop's received frames, and the MS5837 conversion wait (9.5 ms for a
     * 9.04 ms conversion) has no room for that much staleness. */
    const uint64_t now = auvcan_micros(s->can);
    poll_leaks(s, now);                    /* first: a leak edge goes out before anything else */
    poll_depth(s, now);
    poll_slow(s, now);

    const bool depth_fault = (s->ms_phase == MS_FAILED);
    const bool temp_fault  = s->temp_fault;           /* last attempt failed; retried every second */
    const bool hull_fault  = !s->hull_ok;               /* read every 100 ms from the first poll */
    uint16_t code = (uint16_t)(((unsigned)s->leak_stable << SENSOR_CODE_LEAK_SHIFT) & SENSOR_CODE_LEAK_MASK);
    if (depth_fault) code |= SENSOR_CODE_DEPTH_FAULT;
    if (temp_fault)  code |= SENSOR_CODE_TEMP_FAULT;
    if (hull_fault)  code |= SENSOR_CODE_HULL_FAULT;
    /* only our bits: bit 0 (killed) belongs to the thruster module */
    auvcan_set_status_code(s->can, (uint16_t)((s->can->status_code & ~SENSOR_CODE_MASK) | code));
    s->warning = depth_fault || temp_fault || hull_fault;
}

bool sensor_warning(const sensor_t *s)
{
    return s->warning;
}
