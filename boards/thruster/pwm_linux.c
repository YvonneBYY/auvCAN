/*
 * pwm_linux.c - ESC outputs for the Linux build: prints the eight pulse widths.
 * Rate-limited to 5 lines/s, but a change to or from all-neutral prints at once
 * so kill edges are visible with their exact time.
 */
#define _DEFAULT_SOURCE
#include "pwm.h"

#include <stdio.h>
#include <time.h>

#include "board_config.h"

#define PRINT_PERIOD_MS 200u

static uint16_t s_us[THRUSTER_CHANNELS];
static uint16_t s_printed[THRUSTER_CHANNELS];
static uint64_t s_t0_ms, s_last_print_ms;
static thruster_pwm_t s_pwm;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static bool all_neutral(const uint16_t *us)
{
    for (int i = 0; i < THRUSTER_CHANNELS; i++) if (us[i] != THRUSTER_NEUTRAL_US) return false;
    return true;
}

static void maybe_print(void)
{
    bool changed = false;
    for (int i = 0; i < THRUSTER_CHANNELS; i++) changed |= (s_us[i] != s_printed[i]);
    if (!changed) return;
    const uint64_t now = now_ms();
    const bool edge = all_neutral(s_us) != all_neutral(s_printed);
    if (!edge && now - s_last_print_ms < PRINT_PERIOD_MS) return;
    s_last_print_ms = now;
    printf("[%s] t=%7.3fs pwm", BOARD_APP_NAME, (double)(now - s_t0_ms) / 1000.0);
    for (int i = 0; i < THRUSTER_CHANNELS; i++) printf(" %4u", s_us[i]);
    printf(" us\n");
    fflush(stdout);
    for (int i = 0; i < THRUSTER_CHANNELS; i++) s_printed[i] = s_us[i];
}

static void write_us(void *ctx, uint8_t ch, uint16_t us)
{
    (void)ctx;
    if (ch >= THRUSTER_CHANNELS) return;
    s_us[ch] = us;
    if (ch == THRUSTER_CHANNELS - 1) maybe_print();       /* one line per full update */
}

const thruster_pwm_t *pwm_init(void)
{
    s_t0_ms = now_ms();
    for (int i = 0; i < THRUSTER_CHANNELS; i++) s_us[i] = s_printed[i] = THRUSTER_NEUTRAL_US;
    s_pwm = (thruster_pwm_t){ .ctx = NULL, .write_us = write_us };
    return &s_pwm;
}
