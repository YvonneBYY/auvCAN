/*
 * pwm_g4.c - ESC outputs on STM32G4 timers.
 *
 * 8 outputs, listed in thruster order by BOARD_PWM_OUTPUTS (board_config.h) as
 * { &timer, TIM_CHANNEL_x }. A G4 timer has at most 4 PWM channels, so the outputs
 * span several timers; each one listed in BOARD_PWM_TIMERS is set up once here:
 *   tick   = 1 us        (PSC = BOARD_PWM_TIM_CLOCK_HZ / 1 MHz - 1)
 *   period = 2000 us     (ARR = 1999), Orion's 500 Hz
 *   CCR    = pulse in us (1100..1900, 1500 neutral)
 * CubeMX: enable every timer in BOARD_PWM_TIMERS with its channels set to
 * "PWM Generation CHx"; prescaler/period there don't matter.
 * CCR and ARR preload are enabled so a new pulse width takes effect at the next
 * period boundary: no runt or stretched pulse when the ramp writes mid-period.
 *
 * SCOPE THIS BEFORE CONNECTING AN ESC: 1500 us high every 2 ms on all eight
 * pins at boot, and the same 1500 us within one period of a kill.
 */
#include "pwm.h"

#include "main.h"                  /* CubeMX: HAL + TIM handles */
#include "board_config.h"

#ifndef HAL_TIM_MODULE_ENABLED
#error "No timer enabled in CubeMX: enable the timers in BOARD_PWM_TIMERS (board_config.h) with their channels as 'PWM Generation CHx' and regenerate"
#endif

#define PWM_PERIOD_US   2000u

#define DECLARE_TIM(h) extern TIM_HandleTypeDef h;
BOARD_PWM_TIMERS(DECLARE_TIM)
#define LIST_TIM(h) &h,

typedef struct { TIM_HandleTypeDef *tim; uint32_t channel; } pwm_out_t;

static const pwm_out_t s_out[] = BOARD_PWM_OUTPUTS;     /* sized by the table, checked below */
static TIM_HandleTypeDef *const s_timers[] = { BOARD_PWM_TIMERS(LIST_TIM) };
static thruster_pwm_t s_pwm;

_Static_assert(sizeof(s_out) / sizeof(s_out[0]) == THRUSTER_CHANNELS, "BOARD_PWM_OUTPUTS needs one entry per thruster");
_Static_assert(BOARD_PWM_TIM_CLOCK_HZ % 1000000u == 0u, "timer clock must be a whole number of MHz");
_Static_assert(BOARD_PWM_TIM_CLOCK_HZ / 1000000u <= 65536u, "prescaler overflows 16 bits");

static void write_us(void *ctx, uint8_t ch, uint16_t us)
{
    (void)ctx;
    if (ch >= THRUSTER_CHANNELS) return;
    __HAL_TIM_SET_COMPARE(s_out[ch].tim, s_out[ch].channel, us);
}

const thruster_pwm_t *pwm_init(void)
{
    for (size_t t = 0; t < sizeof(s_timers) / sizeof(s_timers[0]); t++) {
        TIM_HandleTypeDef *h = s_timers[t];
        __HAL_TIM_SET_PRESCALER(h, BOARD_PWM_TIM_CLOCK_HZ / 1000000u - 1u);
        __HAL_TIM_SET_AUTORELOAD(h, PWM_PERIOD_US - 1u);
        h->Instance->CR1 |= TIM_CR1_ARPE;   /* buffer ARR too (G4 HAL has no macro for this) */
    }
    for (uint8_t i = 0; i < THRUSTER_CHANNELS; i++) {
        __HAL_TIM_SET_COMPARE(s_out[i].tim, s_out[i].channel, THRUSTER_NEUTRAL_US);
        __HAL_TIM_ENABLE_OCxPRELOAD(s_out[i].tim, s_out[i].channel);
    }
    for (size_t t = 0; t < sizeof(s_timers) / sizeof(s_timers[0]); t++) {
        s_timers[t]->Instance->EGR = TIM_EGR_UG;    /* load PSC/ARR/CCR now, not after the first period */
    }
    for (uint8_t i = 0; i < THRUSTER_CHANNELS; i++) {
        if (HAL_TIM_PWM_Start(s_out[i].tim, s_out[i].channel) != HAL_OK) return NULL;   /* sets MOE on TIM1/8 */
    }
    s_pwm = (thruster_pwm_t){ .ctx = NULL, .write_us = write_us };
    return &s_pwm;
}
