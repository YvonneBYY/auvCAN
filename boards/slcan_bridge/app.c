/*
 * slcan_bridge/app.c - forward CAN frames between the bus and a host speaking SLCAN.
 *
 *   host --SLCAN text over UART--> [this board] --FDCAN--> bus, and back
 *
 * Same file for the G4 build (bsp_g4.c + stream_g4.c + the G4 FDCAN port) and the
 * Linux test build (bsp_linux.c + stream_linux.c + the mcast port).
 *
 *   status LED      on while the host has the channel open ('O'), off when closed
 *   heartbeat LED   1 Hz = running (bsp)
 *
 * Frames from the host wait in a 64-frame queue for the 3 FDCAN TX buffers, so a
 * multi-frame transfer sent in one burst is not refused. Frames from the bus go
 * out only while the channel is open; when the UART can't keep up they are dropped
 * and the 'F' command reports it (SLCAN_F_RX_FULL).
 */
#include "app.h"

#include <string.h>

#include "board_config.h"
#include "bsp.h"
#include "slcan.h"
#include "stream.h"
#include "auvcan_port.h"

#define TXQ_SIZE        64u          /* host -> bus queue (power of two) */
#define RX_PER_POLL     16u          /* bus frames forwarded per loop iteration */
#define READ_CHUNK      128u

static const bridge_stream_t *s_link;
static const auvcan_port_t   *s_can;
static slcan_t                s_slcan;

static CanardCANFrame s_txq[TXQ_SIZE];
static uint32_t       s_txq_head, s_txq_tail;

static auvcan_bus_stats_t s_last;    /* counters at the previous 'F' */

/* diagnostics (debugger) */
uint32_t bridge_to_host, bridge_to_bus, bridge_host_drops, bridge_tx_refused;

static int q_send(void *ctx, const CanardCANFrame *f)
{
    (void)ctx;
    if (s_txq_head - s_txq_tail >= TXQ_SIZE) return 0;                    /* queue full */
    s_txq[s_txq_head++ & (TXQ_SIZE - 1u)] = *f;
    return 1;
}

static void link_write(void *ctx, const char *s, size_t n)
{
    (void)ctx;
    s_link->write(s_link->ctx, s, n);
}

static uint8_t port_flags(void *ctx)
{
    (void)ctx;
    auvcan_bus_stats_t st;
    memset(&st, 0, sizeof(st));
    if (s_can->get_stats != NULL) s_can->get_stats(s_can->ctx, &st);
    uint8_t f = 0;
    if (st.error_passive) f |= SLCAN_F_ERR_PASSIVE;
    if (st.tec >= 96u || st.rec >= 96u) f |= SLCAN_F_ERR_WARNING;
    if (st.bus_off_count != s_last.bus_off_count) f |= SLCAN_F_BUS_ERROR;
    if (st.rx_overflow != s_last.rx_overflow) f |= SLCAN_F_OVERRUN;
    s_last = st;
    return f;
}

static void drain_txq(void)
{
    while (s_txq_tail != s_txq_head) {
        const int r = s_can->tx(s_can->ctx, &s_txq[s_txq_tail & (TXQ_SIZE - 1u)]);
        if (r == 0) break;                         /* hardware full: retry next loop, keep order */
        if (r < 0) bridge_tx_refused++;
        else bridge_to_bus++;
        s_txq_tail++;
    }
}

void app_setup(void)
{
    s_link = stream_init();
    if (s_link == NULL) bsp_fatal("stream_init: host link not set up (UART+DMA or USB CDC, see board_config.h)");

    s_can = bsp_init();
    if (s_can == NULL) bsp_fatal("bsp_init: CAN port (check BOARD_FDCAN_CLOCK_HZ)");
    bsp_led(false);

    /* One filter that matches every extended ID: mask 0. */
    const auvcan_filter_t all = { .id = 0u, .mask = 0u, .high_priority = false };
    if (s_can->start(s_can->ctx, &all, 1u) != 0) bsp_fatal("CAN start");

    const slcan_io_t io = { .ctx = NULL, .send_frame = q_send, .write = link_write, .port_flags = port_flags };
    slcan_init(&s_slcan, &io);
}

void app_loop(void)
{
    /* host -> bus */
    uint8_t buf[READ_CHUNK];
    size_t n;
    while ((n = s_link->read(s_link->ctx, buf, sizeof(buf))) > 0) {
        slcan_feed(&s_slcan, buf, n);
    }
    drain_txq();

    /* bus -> host */
    CanardCANFrame f;
    for (unsigned i = 0; i < RX_PER_POLL && s_can->rx(s_can->ctx, &f); i++) {
        if (!s_slcan.open) continue;                        /* closed: discard */
        if (s_link->room(s_link->ctx) < SLCAN_FRAME_TEXT) {
            bridge_host_drops++;
            slcan_note(&s_slcan, SLCAN_F_RX_FULL);
            continue;
        }
        char line[SLCAN_FRAME_TEXT];
        s_link->write(s_link->ctx, line, slcan_format(&f, line));
        bridge_to_host++;
    }
    s_link->poll(s_link->ctx);

    if (s_can->micros != NULL) bsp_heartbeat(s_can->micros(s_can->ctx));
    bsp_led(s_slcan.open);
}
