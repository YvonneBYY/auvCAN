/*
 * sim_chips.h - software models of the sensor board's chips, behind the same
 * sensor_hw_t as the real board: MS5837 (reset, PROM with a valid CRC, D1/D2
 * conversions that read 0 if fetched too early), MCP9808 (T_A and manufacturer
 * ID), the hull-pressure ADC (junk byte + 16-bit result) and 4 leak inputs.
 *
 * Used by tests/test_sensor.c and by the Linux build (hw_linux.c). A missing
 * chip NACKs, like an unplugged breakout.
 */
#ifndef SIM_CHIPS_H
#define SIM_CHIPS_H

#include <stdbool.h>
#include <stdint.h>

#include "sensor.h"

typedef struct {
    const uint64_t *now_us;          /* the owner's clock: conversions take 9.04 ms */

    /* MS5837 */
    bool            ms_present;
    ms5837_model_t  model;
    uint16_t        prom[7];
    float           pressure_pa;     /* what the chip "feels"; water temp is 20.00 °C */
    uint8_t         ms_reg;          /* last command */
    uint8_t         ms_pending;      /* 1 = D1, 2 = D2 converting */
    uint8_t         ms_early_reads;  /* test knob: the next N ADC reads answer 0 (not finished) */
    uint64_t        ms_conv_start_us;
    uint32_t        ms_result;       /* ADC register after the conversion */

    /* MCP9808 */
    bool            mcp_present;
    uint8_t         mcp_addr;
    uint8_t         mcp_reg;
    float           mcp_temp_c;

    /* hull ADC */
    bool            adc_present;     /* false: MISO floats high -> 0xFFFF */
    uint16_t        adc_raw;

    uint8_t         leaks;           /* bit i = leak input i */
} sim_chips_t;

/* All chips present; 02BA or 30BA PROM from the datasheet example; 1 atm everywhere. */
void sim_chips_init(sim_chips_t *c, const uint64_t *now_us, ms5837_model_t model, uint8_t mcp_addr);
void sim_chips_set_depth(sim_chips_t *c, float depth_m, float surface_pa, float water_density);
uint16_t sim_hull_raw_for_atm(float atm);             /* inverse of hull_atm_from_raw */
sensor_hw_t sim_chips_hw(sim_chips_t *c);

#endif
