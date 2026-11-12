/*
 * statuslog.c - see statuslog.h. Portable: no HAL, output through bsp_log().
 */
#include "statuslog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include <uavcan.protocol.NodeStatus.h>

#define PERIOD_US     1000000u
#define LOST_US       3000000u
#define MAX_NODES     12u

typedef struct {
    uint8_t  id;            /* 0 = free slot */
    uint8_t  health;
    uint16_t code;
    uint64_t last_us;
} peer_t;

static peer_t   s_peers[MAX_NODES];
static uint64_t s_next_us;
static uint32_t s_last_rx, s_last_tx, s_last_busoff;
static statuslog_extra_fn s_extra;
static void              *s_extra_user;

void statuslog_set_extra(statuslog_extra_fn fn, void *user)
{
    s_extra = fn;
    s_extra_user = user;
}

static void on_node_status(auvcan_t *can, CanardRxTransfer *t, void *user)
{
    (void)user;
    struct uavcan_protocol_NodeStatus ns;
    if (t->source_node_id == can->cfg.node_id) return;          /* our own echo (virtual bus) */
    if (uavcan_protocol_NodeStatus_decode(t, &ns)) return;

    peer_t *slot = NULL;
    for (unsigned i = 0; i < MAX_NODES; i++) {
        if (s_peers[i].id == t->source_node_id) { slot = &s_peers[i]; break; }
        if (slot == NULL && s_peers[i].id == 0u) slot = &s_peers[i];
    }
    if (slot == NULL) return;                       /* table full: more than 12 nodes */
    slot->id      = t->source_node_id;
    slot->health  = ns.health;
    slot->code    = ns.vendor_specific_status_code;
    slot->last_us = can->poll_now_us;
}

void statuslog_init(auvcan_t *can)
{
    memset(s_peers, 0, sizeof(s_peers));
    (void)auvcan_subscribe(can, UAVCAN_PROTOCOL_NODESTATUS_ID, UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                           on_node_status, NULL);
}

/* snprintf that never runs past the end; returns the new length */
__attribute__((format(printf, 4, 5)))
static size_t add(char *buf, size_t size, size_t n, const char *fmt, ...)
{
    if (n + 1u >= size) return n;
    va_list ap;
    va_start(ap, fmt);
    const int k = vsnprintf(buf + n, size - n, fmt, ap);
    va_end(ap);
    if (k < 0) return n;
    n += (size_t)k;
    return (n >= size) ? size - 1u : n;
}
#define ADD(...) (n = add(buf, size, n, __VA_ARGS__))

static const char *health_name(uint8_t h)
{
    switch (h) {
    case UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK:      return "ok";
    case UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING: return "WARN";
    case UAVCAN_PROTOCOL_NODESTATUS_HEALTH_ERROR:   return "ERR";
    default:                                        return "CRIT";
    }
}

size_t statuslog_format(auvcan_t *can, uint64_t now_us, char *buf, size_t size)
{
    size_t n = 0;
    if (size == 0u) return 0;
    buf[0] = '\0';

    auvcan_bus_stats_t st;
    auvcan_get_bus_stats(can, &st);

    const uint64_t up_ds = (now_us - can->boot_us) / 100000u;     /* tenths of a second */
    const uint32_t rx = can->rx_frames - s_last_rx;
    const uint32_t tx = can->tx_frames - s_last_tx;
    const uint32_t new_busoff = st.bus_off_count - s_last_busoff;
    s_last_rx = can->rx_frames;
    s_last_tx = can->tx_frames;
    s_last_busoff = st.bus_off_count;

    const char *bus = (new_busoff > 0u) ? "BUSOFF"
                    : (st.error_passive || st.tec >= 128u || st.rec >= 128u) ? "PASSIVE" : "ACTIVE";

    ADD("t=%lu.%lus n%u %s | bus %s TEC=%u REC=%u busoff=%lu ovf=%lu txerr=%lu | rx %lu/s tx %lu/s | nodes",
        (unsigned long)(up_ds / 10u), (unsigned long)(up_ds % 10u),
        (unsigned)can->cfg.node_id, (can->status_code & 1u) ? "KILLED" : "ARMED",
        bus, (unsigned)st.tec, (unsigned)st.rec, (unsigned long)st.bus_off_count,
        (unsigned long)st.rx_overflow, (unsigned long)st.tx_errors,
        (unsigned long)rx, (unsigned long)tx);

    bool any = false;
    for (unsigned i = 0; i < MAX_NODES; i++) {
        const peer_t *p = &s_peers[i];
        if (p->id == 0u) continue;
        any = true;
        if (now_us - p->last_us > LOST_US) {
            ADD(" %u:LOST", (unsigned)p->id);
        } else {
            ADD(" %u:%s%s", (unsigned)p->id, health_name(p->health), (p->code & 1u) ? ",K" : "");
        }
    }
    if (!any) ADD(" none");

    if (s_extra != NULL && n + 4u < size) {
        ADD(" | ");
        const size_t k = s_extra(buf + n, size - n, s_extra_user);
        n = (n + k < size) ? n + k : size - 1u;
        buf[n] = '\0';
    }

    /* The patterns met on the bench so far, in the order worth checking. */
    if (new_busoff > 0u) {
        ADD(" | hint: bit errors -> MCU<->transceiver (TX/RX swapped, no 3.3V, standby pin high)");
    } else if (st.tec >= 128u && st.rec == 0u && rx == 0u) {
        ADD(" | hint: no ACK, nothing heard -> CANH/CANL not joined to another node, or it's off");
    } else if (st.rec >= 96u) {
        ADD(" | hint: receive errors -> bit rate/clock mismatch, termination, CANH/CANL swapped");
    }
    ADD("\r\n");
    return n;
}

void statuslog_poll(auvcan_t *can)
{
    const uint64_t now = can->poll_now_us;
    bsp_log_poll();
    if (now < s_next_us) return;
    s_next_us = now + PERIOD_US;

    char line[448];
    const size_t n = statuslog_format(can, now, line, sizeof(line));
    bsp_log(line, n);
}
