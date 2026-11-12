/*
 * sensor.h - sensor board logic (node 30), independent of the MCU.
 *
 * Ported from Orion's Sensor board (Orion/Sensor/sensor.c), same parts:
 *   depth + water temp   MS5837 on I2C (0x76), 02BA or 30BA (cfg.model)
 *   internal temp        MCP9808 on I2C (0x18)
 *   hull pressure        analog sensor on an external 16-bit SPI ADC
 *   leaks                4 digital inputs
 *
 * Outputs (all broadcasts, nothing received; the board ignores Kill):
 *   cuauv.Depth       50 Hz, AUVCAN_PRIO_SENSOR_FAST
 *   cuauv.HullStatus  10 Hz at AUVCAN_PRIO_TELEM_SLOW, and IMMEDIATELY at
 *                     AUVCAN_PRIO_HULL_EVENT when a (debounced) leak input changes
 *   NodeStatus        health WARNING while any sensor has failed; status code:
 *                     bits 0-3 = leak[0..3], bit 4 = depth fault, bit 5 = internal
 *                     temp fault, bit 6 = hull pressure fault
 *
 * A failed sensor publishes NaN for its values and clears its *_ok flag; the
 * board keeps retrying it (MS5837 / MCP9808 once a second).
 *
 * Changes from Orion:
 *   1. MS5837 at OSR 4096 (Orion: OSR 256, the noisiest) and non-blocking: the
 *      two conversions fit one 20 ms Depth period. Orion blocked 2 x 20 ms.
 *   2. MS5837 PROM CRC is checked, and both the 02BA and 30BA formulas exist.
 *      Orion's are the 02BA set: a 30BA (Blue Robotics Bar30) needs cfg.model.
 *   3. MCP9808 below 0 °C: Orion (copying the datasheet's sample code) returned
 *      the magnitude with the wrong sign. Here the 13-bit two's complement is used.
 *   4. Leak inputs are debounced (3 equal samples, 10 ms apart).
 *   5. Hull pressure: same conversion and same SPI framing (one junk byte, then the
 *      16-bit result MSB first). Check the framing against the ADC's datasheet.
 *
 * Usage (see app.c):
 *     sensor_init(&s, &can, &cfg, &hw)   before auvcan_start()
 *     loop: auvcan_poll(&can); sensor_poll(&s);
 */
#ifndef SENSOR_H
#define SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "auvcan.h"

#define SENSOR_LEAKS              4
#define SENSOR_DEPTH_PERIOD_US    20000u    /* 50 Hz */
#define SENSOR_SLOW_PERIOD_US     100000u   /* 10 Hz: HullStatus, internal temp, hull pressure */
#define SENSOR_LEAK_SAMPLE_US     10000u
#define SENSOR_LEAK_DEBOUNCE      3u        /* equal samples needed to accept a change */
#define SENSOR_RETRY_US           1000000u  /* re-init a failed I2C sensor */

/* Status code bits (NodeStatus.vendor_specific_status_code). Bit 0 = killed belongs to
 * the thruster module on the same board; this module owns bits 1-7. */
#define SENSOR_CODE_LEAK_SHIFT    1u
#define SENSOR_CODE_LEAK_MASK     0x001Eu   /* leak i = bit (1 + i) */
#define SENSOR_CODE_DEPTH_FAULT   0x0020u
#define SENSOR_CODE_TEMP_FAULT    0x0040u
#define SENSOR_CODE_HULL_FAULT    0x0080u
#define SENSOR_CODE_MASK          0x00FEu

/* The board's I/O. Every function returns 0 on success. */
typedef struct {
    void   *ctx;
    int     (*i2c_write)(void *ctx, uint8_t addr7, const uint8_t *data, uint8_t n);
    int     (*i2c_read)(void *ctx, uint8_t addr7, uint8_t *data, uint8_t n);
    int     (*spi_read)(void *ctx, uint8_t *rx, uint8_t n);    /* CS low, n bytes, CS high */
    uint8_t (*leak_raw)(void *ctx);                             /* bit i = input i reads "leak" */
} sensor_hw_t;

typedef enum { MS5837_02BA = 2, MS5837_30BA = 30 } ms5837_model_t;

typedef struct {
    ms5837_model_t model;
    float          water_density;   /* kg/m^3: 1000 fresh (pool), ~1025 sea */
    float          surface_pa;      /* pressure at depth 0 (Orion: 101325) */
    uint8_t        mcp9808_addr;    /* 0x18 with A2..A0 low */
} sensor_cfg_t;

typedef enum { MS_RESET, MS_RESET_WAIT, MS_IDLE, MS_WAIT_D1, MS_WAIT_D2, MS_FAILED } ms_phase_t;

typedef struct {
    auvcan_t     *can;
    sensor_cfg_t  cfg;
    sensor_hw_t   hw;

    /* MS5837 */
    ms_phase_t    ms_phase;
    uint16_t      prom[7];
    uint32_t      d1;
    uint64_t      ms_due_us;        /* next state-machine step */
    uint64_t      next_depth_us;    /* next 20 ms Depth slot */
    bool          depth_ok;
    float         depth_m, water_temp_c;

    /* slow sensors */
    uint64_t      next_slow_us;
    uint64_t      temp_retry_us;
    bool          temp_ok, temp_id_checked, temp_fault;
    float         internal_temp_c;
    bool          hull_ok;
    uint16_t      hull_raw;
    float         hull_atm;

    /* leaks */
    uint64_t      next_leak_us;
    uint8_t       leak_stable, leak_candidate, leak_count;

    uint8_t       depth_tid, hull_tid;

    /* diagnostics */
    uint32_t      i2c_errors, crc_errors, depth_published, hull_published, leak_events;
    uint32_t      adc_early;        /* MS5837 read before its conversion finished (restarted) */
    uint8_t       ms_early_run;     /* ...in a row, for the current reading */
    bool          warning;          /* a sensor has failed (see sensor_warning) */
} sensor_t;

int  sensor_init(sensor_t *s, auvcan_t *can, const sensor_cfg_t *cfg, const sensor_hw_t *hw);
void sensor_poll(sensor_t *s);
/* True while a sensor has failed. The app turns this (with anything else the board
 * checks) into NodeStatus health WARNING; this module doesn't set health. */
bool sensor_warning(const sensor_t *s);

/* ---- Pure helpers (exposed for unit tests and the simulator) ---------------- */
uint8_t ms5837_crc4(const uint16_t prom[7]);                  /* expected value of prom[0] >> 12 */
/* First + second order compensation from the datasheet. temp in 0.01 °C, pressure in Pa. */
void    ms5837_compute(ms5837_model_t model, const uint16_t prom[7], uint32_t d1, uint32_t d2,
                       int32_t *temp_centi_c, int32_t *pressure_pa);
float   mcp9808_celsius(uint8_t upper, uint8_t lower);        /* T_A register, 0.0625 °C */
float   hull_atm_from_raw(uint16_t raw);                      /* Orion's sensor formula */

#endif
