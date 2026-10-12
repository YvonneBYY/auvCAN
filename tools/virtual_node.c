/*
 * virtual_node - run the real auvCAN core as a board on a PC (mcast:0 bus).
 *
 *   ./build/virtual_node <node_id> <name> [--actuator]
 *
 * NodeStatus vendor_specific_status_code bit 0 = killed, so bench.py / the
 * GUI Tool can see the kill state from outside. Exits with code 42 on a
 * valid RestartNode (stands in for the MCU reset).
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "auvcan.h"
#include "auvcan_port_mcast.h"

static volatile sig_atomic_t g_stop;
static void on_sig(int s) { (void)s; g_stop = 1; }

static void on_kill_change(auvcan_t *can, bool killed, void *user)
{
    (void)user;
    auvcan_set_status_code(can, killed ? 1u : 0u);
    printf("[node %u] t=%8.3fs %s\n", can->cfg.node_id,
           (double)(auvcan_micros(can) - can->boot_us) / 1e6, killed ? "KILLED" : "ARMED");
    fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <node_id> <name> [--actuator]\n", argv[0]);
        return 2;
    }
    const bool actuator = (argc > 3 && strcmp(argv[3], "--actuator") == 0);

    static mcast_port_t mp;
    static auvcan_t can;
    const auvcan_port_t *port = mcast_port_init(&mp, 0, (uint8_t)atoi(argv[1]));
    if (port == NULL) return 1;

    const auvcan_config_t cfg = {
        .node_id = (uint8_t)atoi(argv[1]), .name = argv[2],
        .hw_major = 1, .sw_major = 0, .sw_minor = 1, .sw_vcs_commit = 0xA0C0A11u,
        .kill_timeout_ms = actuator ? AUVCAN_KILL_TIMEOUT_MS : 0u,
        .on_kill_change = on_kill_change,
        .port = port,
    };
    if (auvcan_init(&can, &cfg) != 0 || auvcan_start(&can) != 0) {
        fprintf(stderr, "init failed\n");
        return 1;
    }
    auvcan_set_status_code(&can, actuator ? 1u : 0u);
    printf("[node %u] up as %s%s\n", cfg.node_id, cfg.name, actuator ? " (actuator, KILLED)" : "");
    fflush(stdout);

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    while (!g_stop) {
        auvcan_poll(&can);
        if (mp.reboot_requested) {
            printf("[node %u] RestartNode accepted -> reboot\n", cfg.node_id);
            fflush(stdout);
            return 42;
        }
        usleep(500);
    }
    return 0;
}
