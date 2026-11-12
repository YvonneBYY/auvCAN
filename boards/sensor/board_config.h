/*
 * board_config.h - the sensor board (node 30): depth (MS5837), internal temperature
 * (MCP9808), hull pressure (analog sensor on an SPI ADC), 4 leak inputs, AND the
 * 8 thruster ESC outputs (T1-T8 = RawCommand.cmd[0..7]; the thruster board is retired).
 * Values marked "param" become uavcan.protocol.param entries in stage 3.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "auvcan_spec.h"

/* Hardware profile for the shared bench BSP (LED, heartbeat, FDCAN pins).
 * Until the sensor PCB exists, the Deft Controls PCB stands in. */
#if defined(BOARD_HW_NUCLEO_G431KB)
#include "hw_nucleo_g431kb.h"
#elif !defined(BOARD_NO_HW_PROFILE)
#include "hw_deft_controls_pcb.h"
#endif

#define BOARD_NODE_ID         AUVCAN_NODE_SENSOR
#define BOARD_NAME            "edu.cornell.cuauv.sensor"
#define BOARD_APP_NAME        "sensor"

#define BOARD_HW_MAJOR        0
#define BOARD_HW_MINOR        1
#define BOARD_SW_MAJOR        0
#define BOARD_SW_MINOR        1
#ifndef BOARD_GIT_HASH
#define BOARD_GIT_HASH        0u
#endif

#ifndef BOARD_FDCAN_CLOCK_HZ
#define BOARD_FDCAN_CLOCK_HZ  24000000u
#endif

/* ---- Sensors (params) --------------------------------------------------------
 * MS5837 variant: Orion's formulas are the 02BA set (2 bar, ~10 m). A Blue Robotics
 * Bar30 is the 30BA: set SENSOR_MS5837_MODEL to MS5837_30BA, or depth is wrong. */
#ifndef SENSOR_MS5837_MODEL
#define SENSOR_MS5837_MODEL   MS5837_02BA
#endif
#define SENSOR_WATER_DENSITY  1000.0f     /* kg/m^3: fresh water (pool); ~1025 for sea water */
#define SENSOR_SURFACE_PA     101325.0f   /* depth 0 (Orion's value) */
#define SENSOR_MCP9808_ADDR   0x18u       /* A2..A0 low */

/* ---- Thrusters (boards/thruster/thruster.c), params ---------------------------- */
#define THRUSTER_CMD_OFFSET   0                     /* RawCommand.cmd[0..7] = T1-T8 */
#define THRUSTER_POLARITY     { 1, 1, 1, 1, 1, 1, 1, 1 }   /* TODO: measure on the new sub */
/* Which node's esc.RawCommand the thrusters obey. 2 = the host (the sub).
 * 1 = the boss's bench throttle (BOARD_BENCH_THROTTLE): add the symbol
 * THRUSTER_CMD_SOURCE=1 for bench builds only, never on the sub. */
#ifndef THRUSTER_CMD_SOURCE
#define THRUSTER_CMD_SOURCE   AUVCAN_NODE_HOST
#endif
/* Orion defaults (thrusters.toml) */
#define THRUSTER_RAMP_PERIOD_MS  10
#define THRUSTER_RAMP_INC         5
#define THRUSTER_RAMP_DEC         5

/* ---- G4 PWM (boards/thruster/pwm_g4.c) ---------------------------------------
 * 8 outputs, listed in thruster order (T1..T8) as { &timer_handle, TIM_CHANNEL_x }.
 * No G4 timer has more than 4 independent PWM channels, so they span 3 timers; each
 * listed channel must be "PWM Generation CHx" in CubeMX. pwm_g4.c sets prescaler and
 * period itself (1 us ticks, 2000 us = Orion's 500 Hz). BOARD_PWM_TIM_CLOCK_HZ = the
 * timers' input clock, the same for every timer listed (170 MHz in the bench tree).
 * BENCH pin-out on the Deft Controls PCB, clear of the sensor pins below:
 *   T1 TIM1_CH1 PC0   T2 TIM1_CH2 PA9   T3 TIM1_CH3 PA10
 *   T4 TIM2_CH1 PA0   T5 TIM2_CH2 PA1   T6 TIM2_CH3 PA2   T7 TIM2_CH4 PA3
 *   T8 TIM3_CH1 PC6   (was PA6 on the thruster board: PA6 is SPI1 MISO here)
 * Replace with the sensor PCB's pin-out when it exists. */
#ifndef BOARD_PWM_OUTPUTS
#define BOARD_PWM_TIMERS(X)      X(htim1) X(htim2) X(htim3)
#define BOARD_PWM_OUTPUTS        { { &htim1, TIM_CHANNEL_1 }, { &htim1, TIM_CHANNEL_2 }, \
                                   { &htim1, TIM_CHANNEL_3 }, { &htim2, TIM_CHANNEL_1 }, \
                                   { &htim2, TIM_CHANNEL_2 }, { &htim2, TIM_CHANNEL_3 }, \
                                   { &htim2, TIM_CHANNEL_4 }, { &htim3, TIM_CHANNEL_1 } }
#endif
#ifndef BOARD_PWM_TIM_CLOCK_HZ
#define BOARD_PWM_TIM_CLOCK_HZ   170000000u
#endif

/* ---- G4 pins (hw_g4.c): BENCH pin-out on the Deft Controls PCB --------------
 * Avoids CAN (PA8/PA15, PB8/PB9), USB (PA11/PA12), SWD, the LEDs (PC2/PC3/PC7) and
 * the kill input (PB7) and the PWM pins above. Replace with the sensor PCB's pin-out
 * when it exists.
 *   I2C3  SCL PC8, SDA PC9 (AF8)    MS5837 + MCP9808, 400 kHz, 2.2-4.7 kΩ pull-ups to 3V3
 *   SPI1  SCK PA5, MISO PA6, MOSI PA7, CS = PA4 as a GPIO (driven here)
 *   Leak  PC4, PC5, PB0, PB1 inputs; "leak" = high (as on Orion), internal pull-down
 * CubeMX: I2C3 (I2C mode, Fast 400 kHz) and SPI1 (Full-Duplex Master, 8-bit, mode 0,
 * a few MHz or less); the GPIOs (CS, leaks) are set up by hw_g4.c. */
#ifndef BOARD_SENSOR_I2C
#define BOARD_SENSOR_I2C      hi2c3
#endif
#ifndef BOARD_SENSOR_SPI
#define BOARD_SENSOR_SPI      hspi1
#define BOARD_ADC_CS_PORT     GPIOA
#define BOARD_ADC_CS_PIN      GPIO_PIN_4
#endif
#ifndef BOARD_LEAK_PINS
#define BOARD_LEAK_PINS       { { GPIOC, GPIO_PIN_4 }, { GPIOC, GPIO_PIN_5 }, \
                                { GPIOB, GPIO_PIN_0 }, { GPIOB, GPIO_PIN_1 } }
#define BOARD_LEAK_ACTIVE_HIGH 1
#endif

#endif
