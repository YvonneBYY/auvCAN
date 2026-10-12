/*
 * bsp_linux.c - run a bench app as a Linux process on the "mcast:0" virtual bus.
 *
 *   ./build/bench_controller_linux &      # prints its pid
 *   kill -USR1 <pid>                      # = press the button once
 *
 * LED changes are printed with a timestamp. A valid RestartNode exits with
 * code 42 (stands in for the MCU reset). Also provides main().
 */
#define _DEFAULT_SOURCE
#include "bsp.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "board_config.h"
#include "auvcan_port_mcast.h"
#include "app.h"

#ifndef BOARD_APP_NAME
#define BOARD_APP_NAME "app"
#endif

#define PRESS_MS 100

static mcast_port_t s_port;
static volatile sig_atomic_t s_press_requests;
static volatile sig_atomic_t s_stop;
static uint64_t s_press_until_ms;
static int s_led = -1;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static uint64_t s_t0;

static void on_usr1(int s) { (void)s; s_press_requests++; }
static void on_stop(int s) { (void)s; s_stop = 1; }

const auvcan_port_t *bsp_init(void)
{
    s_t0 = now_ms();
    return mcast_port_init(&s_port, 0, (uint8_t)BOARD_NODE_ID);
}

void bsp_led(bool on)
{
    if ((int)on == s_led) return;
    s_led = on;
    printf("[%s] t=%7.3fs LED %s\n", BOARD_APP_NAME, (double)(now_ms() - s_t0) / 1000.0, on ? "ON" : "off");
    fflush(stdout);
}

bool bsp_button(void)
{
    if (s_press_requests > 0) {
        s_press_requests--;
        s_press_until_ms = now_ms() + PRESS_MS;
        printf("[%s] t=%7.3fs button pressed\n", BOARD_APP_NAME, (double)(now_ms() - s_t0) / 1000.0);
        fflush(stdout);
    }
    return now_ms() < s_press_until_ms;
}

void bsp_fatal(const char *why)
{
    fprintf(stderr, "[%s] fatal: %s\n", BOARD_APP_NAME, why);
    exit(1);
}

int main(void)
{
    signal(SIGUSR1, on_usr1);
    signal(SIGINT, on_stop);
    signal(SIGTERM, on_stop);
    printf("[%s] pid %d, node %d, bus mcast:0\n", BOARD_APP_NAME, (int)getpid(), BOARD_NODE_ID);
    fflush(stdout);

    app_setup();
    while (!s_stop) {
        app_loop();
        if (s_port.reboot_requested) {
            printf("[%s] RestartNode -> reboot\n", BOARD_APP_NAME);
            return 42;
        }
        usleep(500);
    }
    return 0;
}
