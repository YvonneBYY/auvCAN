/*
 * auvcan_port_sim.h - in-process virtual CAN bus for host-side tests.
 *
 * Every started port on a sim_bus receives every frame another port sends,
 * after passing through an emulation of the G4's 8 extended acceptance filters
 * (including the accept-all fallback when more than 8 are requested).
 * Time is a shared simulated clock advanced by the test.
 */
#ifndef AUVCAN_PORT_SIM_H
#define AUVCAN_PORT_SIM_H

#include "auvcan_port.h"
#include "can_ring.h"

#define SIM_MAX_PORTS    8
#define SIM_HW_FILTERS   8
#define SIM_MAX_FILTERS 16

typedef struct sim_bus sim_bus_t;

typedef struct {
    sim_bus_t         *bus;
    can_ring_t         rx;
    auvcan_filter_t    filters[SIM_MAX_FILTERS];
    uint8_t            n_filters;
    bool               started;
    bool               tx_blocked;      /* test hook: simulate full hardware TX */
    uint8_t            uid[16];
    uint32_t           reboots;
    uint32_t           frames_tx;
    uint32_t           frames_rx;       /* frames that passed the "hardware" filter */
    auvcan_bus_stats_t inject;          /* test hook: stats the port reports */
    auvcan_port_t      port;
} sim_port_t;

struct sim_bus {
    sim_port_t *ports[SIM_MAX_PORTS];
    int         n;
    uint64_t    now_us;
    uint32_t    advance_per_read_us;   /* test hook: clock moves on every micros() call */
};

void sim_bus_init(sim_bus_t *bus);
const auvcan_port_t *sim_port_init(sim_port_t *p, sim_bus_t *bus, uint8_t uid_seed);

#endif
