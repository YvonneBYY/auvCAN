#ifndef AUVCAN_PORT_MCAST_H
#define AUVCAN_PORT_MCAST_H

#include "auvcan_port.h"

#define MCAST_MAX_FILTERS 16

typedef struct {
    int             rx_fd, tx_fd;
    uint16_t        tx_port;          /* network order; used to drop our own loopback */
    auvcan_filter_t filters[MCAST_MAX_FILTERS];
    uint8_t         n_filters;
    bool            started;
    bool            reboot_requested;
    uint8_t         uid[16];
    uint32_t        frames_rx;
    auvcan_port_t   port;
} mcast_port_t;

/* bus = N in "mcast:N". Returns NULL on socket errors. */
const auvcan_port_t *mcast_port_init(mcast_port_t *p, int bus, uint8_t uid_seed);

#endif
