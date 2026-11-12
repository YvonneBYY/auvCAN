/*
 * auvcan_port_mcast.c - Linux port: CAN over UDP multicast, wire-compatible
 * with pydronecan's "mcast:N" driver (239.65.82.N:57732).
 *
 * Lets you run the real auvCAN core as a virtual board on a PC and talk to it
 * with tools/bench.py or the DroneCAN GUI Tool (interface "mcast:0"), before
 * any hardware exists. Emulates the G4's 8 extended acceptance filters so the
 * filter values the core computes are exercised against real DroneCAN traffic.
 *
 * Packet: u16 magic 0x2934 | u16 crc16-ccitt(rest) | u16 flags | u32 id (bit31 = EXT) | data
 */
#define _DEFAULT_SOURCE
#include "auvcan_port_mcast.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MCAST_BASE   "239.65.82."
#define MCAST_PORT   57732
#define MCAST_MAGIC  0x2934u
#define HW_FILTERS   8u

static uint16_t crc16_ccitt(const uint8_t *d, size_t n)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)(d[i] << 8);
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

//if incoming message's sender id is accpeted by the node's filter
static bool accepts(const mcast_port_t *p, uint32_t id)
{
    const uint8_t n_hw = (p->n_filters > HW_FILTERS) ? (uint8_t)HW_FILTERS : p->n_filters;
    for (uint8_t i = 0; i < n_hw; i++) {
        if ((id & p->filters[i].mask) == (p->filters[i].id & p->filters[i].mask)) return true;
    }
    return p->n_filters > HW_FILTERS;
}

//store the filters to emulate them
static int m_start(void *ctx, const auvcan_filter_t *f, uint8_t n)
{
    mcast_port_t *p = ctx;
    if (n > MCAST_MAX_FILTERS) return -1;
    memcpy(p->filters, f, n * sizeof(*f)); //copy the filters into the port's emulated filter table
    p->n_filters = n;
    p->started = true;
    return 0;
}

//send a upd
static int m_tx(void *ctx, const CanardCANFrame *f)
{
    mcast_port_t *p = ctx;
    uint8_t pkt[10 + 8];
    const uint32_t id = (f->id & CANARD_CAN_EXT_ID_MASK) | ((f->id & CANARD_CAN_FRAME_EFF) ? (1u << 31) : 0u);
    pkt[0] = MCAST_MAGIC & 0xFFu; pkt[1] = MCAST_MAGIC >> 8;
    pkt[4] = 0; pkt[5] = 0;                                   /* flags: classic CAN */
    pkt[6] = (uint8_t)id; pkt[7] = (uint8_t)(id >> 8); pkt[8] = (uint8_t)(id >> 16); pkt[9] = (uint8_t)(id >> 24);
    memcpy(&pkt[10], f->data, f->data_len);
    const uint16_t crc = crc16_ccitt(&pkt[4], 6u + f->data_len);
    pkt[2] = (uint8_t)crc; pkt[3] = (uint8_t)(crc >> 8);
    return (send(p->tx_fd, pkt, 10u + f->data_len, 0) >= 0) ? 1 : -1;
}

//read the socket
static bool m_rx(void *ctx, CanardCANFrame *f)
{
    mcast_port_t *p = ctx;
    uint8_t pkt[80];
    struct sockaddr_in from;
    for (;;) {
        socklen_t fl = sizeof(from);
        const ssize_t n = recvfrom(p->rx_fd, pkt, sizeof(pkt), 0, (struct sockaddr *)&from, &fl); //receives one message from a socket and also tells you who sent it
        if (n < 0) return false;                              /* EAGAIN: nothing pending */
        if (from.sin_port == p->tx_port) continue;            /* our own frame looped back */
        if (n < 10 || n > 18) continue;                       /* not classic CAN */
        if ((uint16_t)(pkt[0] | (pkt[1] << 8)) != MCAST_MAGIC) continue;
        if ((uint16_t)(pkt[2] | (pkt[3] << 8)) != crc16_ccitt(&pkt[4], (size_t)n - 4u)) continue;
        const uint32_t raw = (uint32_t)pkt[6] | ((uint32_t)pkt[7] << 8) | ((uint32_t)pkt[8] << 16) | ((uint32_t)pkt[9] << 24);
        if ((raw & (1u << 31)) == 0u) continue;               /* DroneCAN: extended only */
        const uint32_t id = raw & CANARD_CAN_EXT_ID_MASK;
        if (!p->started || !accepts(p, id)) continue;         /* "hardware" filter */
        memset(f, 0, sizeof(*f));
        f->id = id | CANARD_CAN_FRAME_EFF;
        f->data_len = (uint8_t)(n - 10);
        memcpy(f->data, &pkt[10], f->data_len);
        p->frames_rx++;
        return true;
    }
}

static uint64_t m_micros(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void m_uid(void *ctx, uint8_t out[16]) { memcpy(out, ((mcast_port_t *)ctx)->uid, 16); }

static void m_stats(void *ctx, auvcan_bus_stats_t *out) { (void)ctx; memset(out, 0, sizeof(*out)); }

static void m_reboot(void *ctx) { ((mcast_port_t *)ctx)->reboot_requested = true; }

const auvcan_port_t *mcast_port_init(mcast_port_t *p, int bus, uint8_t uid_seed)
{
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < 16; i++) p->uid[i] = (uint8_t)(uid_seed + i);

    char ip[32];
    snprintf(ip, sizeof(ip), MCAST_BASE "%d", bus); //the bus address

    p->rx_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); //creates a socket:
    int one = 1;
    setsockopt(p->rx_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)); //bench and GUI Tool can all listen on port at once
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(MCAST_PORT) }; //swap little endian to big endian (what network uses)
    inet_pton(AF_INET, ip, &a.sin_addr);
    if (bind(p->rx_fd, (struct sockaddr *)&a, sizeof(a)) != 0) { perror("bind"); return NULL; }
    // bind socket to specific address and port

    struct ip_mreq mreq;
    inet_pton(AF_INET, ip, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(p->rx_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) { perror("mcast join"); return NULL; }
    fcntl(p->rx_fd, F_SETFL, O_NONBLOCK); //Makes the socket non-blocking.

    p->tx_fd = socket(AF_INET, SOCK_DGRAM, 0);
    unsigned char ttl = 0;                                    /* 0 = never leave this machine */
    setsockopt(p->tx_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    if (connect(p->tx_fd, (struct sockaddr *)&a, sizeof(a)) != 0) { perror("connect"); return NULL; }
    struct sockaddr_in me; socklen_t ml = sizeof(me);
    getsockname(p->tx_fd, (struct sockaddr *)&me, &ml); //stores the ephemeral port
    p->tx_port = me.sin_port;

    p->port = (auvcan_port_t){
        .ctx = p, .start = m_start, .tx = m_tx, .rx = m_rx, .micros = m_micros,
        .unique_id = m_uid, .get_stats = m_stats, .reboot = m_reboot,
    };
    return &p->port;
}
