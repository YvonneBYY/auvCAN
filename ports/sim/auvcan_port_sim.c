#include "auvcan_port_sim.h"

#include <string.h>

void sim_bus_init(sim_bus_t *bus)
{
    memset(bus, 0, sizeof(*bus));
}

static bool hw_accepts(const sim_port_t *p, uint32_t id)
{
    const uint8_t n_hw = (p->n_filters > SIM_HW_FILTERS) ? SIM_HW_FILTERS : p->n_filters;
    for (uint8_t i = 0; i < n_hw; i++) {
        if ((id & p->filters[i].mask) == (p->filters[i].id & p->filters[i].mask)) {
            return true;
        }
    }
    return p->n_filters > SIM_HW_FILTERS;   /* G4 fallback: accept non-matching into FIFO1 */
}

static int sim_start(void *ctx, const auvcan_filter_t *filters, uint8_t n)
{
    sim_port_t *p = ctx;
    if (n > SIM_MAX_FILTERS) return -1;
    memcpy(p->filters, filters, n * sizeof(*filters));
    p->n_filters = n;
    p->started = true;
    return 0;
}

static int sim_tx(void *ctx, const CanardCANFrame *f)
{
    sim_port_t *p = ctx;
    if (p->tx_blocked) return 0;
    if ((f->id & CANARD_CAN_FRAME_EFF) == 0u) return -1;
    p->frames_tx++;
    const uint32_t id = f->id & CANARD_CAN_EXT_ID_MASK;
    for (int i = 0; i < p->bus->n; i++) {
        sim_port_t *dst = p->bus->ports[i];
        if (dst == p || !dst->started || !hw_accepts(dst, id)) continue;
        dst->frames_rx++;
        (void)can_ring_push(&dst->rx, f);
    }
    return 1;
}

static bool sim_rx(void *ctx, CanardCANFrame *f)
{
    sim_port_t *p = ctx;
    return can_ring_pop(&p->rx, f);
}

static uint64_t sim_micros(void *ctx)
{
    sim_bus_t *b = ((sim_port_t *)ctx)->bus;
    b->now_us += b->advance_per_read_us;
    return b->now_us;
}

static void sim_unique_id(void *ctx, uint8_t out[16])
{
    memcpy(out, ((sim_port_t *)ctx)->uid, 16);
}

static void sim_get_stats(void *ctx, auvcan_bus_stats_t *out)
{
    sim_port_t *p = ctx;
    *out = p->inject;
    out->rx_overflow += p->rx.dropped;
}

static void sim_reboot(void *ctx)
{
    ((sim_port_t *)ctx)->reboots++;
}

const auvcan_port_t *sim_port_init(sim_port_t *p, sim_bus_t *bus, uint8_t uid_seed)
{
    memset(p, 0, sizeof(*p));
    p->bus = bus;
    can_ring_init(&p->rx);
    for (int i = 0; i < 16; i++) p->uid[i] = (uint8_t)(uid_seed + i);
    p->port = (auvcan_port_t){
        .ctx = p, .start = sim_start, .tx = sim_tx, .rx = sim_rx, .micros = sim_micros,
        .unique_id = sim_unique_id, .get_stats = sim_get_stats, .reboot = sim_reboot,
    };
    bus->ports[bus->n++] = p;
    return &p->port;
}
