/*
 * pwm.h - the thruster board's ESC outputs, one implementation per target:
 *   pwm_g4.c     STM32G4 timer, 1 us ticks, 2 ms period
 *   pwm_linux.c  prints pulse widths (for the mcast:0 virtual bus)
 * Both start every channel at 1500 us (neutral) before returning.
 */
#ifndef PWM_H
#define PWM_H

#include "thruster.h"

const thruster_pwm_t *pwm_init(void);     /* NULL on failure */

#endif
