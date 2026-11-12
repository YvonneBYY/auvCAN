/*
 * sensor_hw.h - the sensor board's I/O, one implementation per target:
 *   hw_g4.c     STM32G4: HAL I2C + SPI, CS and leak GPIOs
 *   hw_linux.c  the chips simulated (sim_chips.c), for the mcast:0 virtual bus
 */
#ifndef SENSOR_HW_H
#define SENSOR_HW_H

#include "sensor.h"

const sensor_hw_t *sensor_hw_init(void);      /* NULL on failure */

#endif
