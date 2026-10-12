/*
 * auvcan.h - auvCAN node core: the part every board shares.
 *
 * Provides, on top of libcanard (DroneCAN v0):
 *   - RX/TX pumping between the hardware port and libcanard
 *   - uavcan.protocol.NodeStatus at 1 Hz
 *   - GetNodeInfo and RestartNode servers
 *   - cuauv.Kill subscription with a timeout watchdog (boots KILLED)
 *   - a subscription table for board-specific messages and services
 *   - hardware acceptance filters derived from that table
 *
 * Board usage (same shape as the old serial_init / serial_handle_messages):
 *
 *     static auvcan_t can;
 *     auvcan_init(&can, &cfg);
 *     auvcan_subscribe(&can, ESC_RAWCOMMAND_ID, ESC_RAWCOMMAND_SIGNATURE, on_rawcmd, NULL);
 *     auvcan_start(&can);
 *     for (;;) { auvcan_poll(&can); app_loop(); }
 */
#ifndef AUVCAN_H
#define AUVCAN_H

#include <stdbool.h>
#include <stdint.h>
#include <canard.h>

#include "auvcan_port.h"
#include "auvcan_spec.h"
#include "auvcan_fresh.h"

#ifndef AUVCAN_MAX_SUBS
#define AUVCAN_MAX_SUBS 12
#endif

#ifndef AUVCAN_POOL_BYTES
#define AUVCAN_POOL_BYTES 2048   /* libcanard pool; check peak usage with auvcan_pool_peak() */
#endif

#define AUVCAN_HW_FILTERS 8      /* G4 FDCAN: 8 extended filter elements per instance */

typedef struct auvcan auvcan_t;

typedef void (*auvcan_handler_t)(auvcan_t *can, CanardRxTransfer *transfer, void *user);
typedef void (*auvcan_kill_cb_t)(auvcan_t *can, bool killed, void *user);

typedef struct {
    uint8_t     node_id;            /* AUVCAN_NODE_* */
    const char *name;               /* reverse-DNS style, e.g. "edu.cornell.cuauv.thruster_a" */
    uint8_t     hw_major, hw_minor;
    uint8_t     sw_major, sw_minor;
    uint32_t    sw_vcs_commit;      /* short git hash, 0 if unknown */

    /* Kill handling. 0 = this node ignores cuauv.Kill (sensors, BMB).
     * Actuators set AUVCAN_KILL_TIMEOUT_MS. */
    uint32_t    kill_timeout_ms;
    auvcan_kill_cb_t on_kill_change; /* optional; called on every killed<->armed edge */
    void       *kill_user;

    const auvcan_port_t *port;
} auvcan_config_t;

typedef struct {
    uint16_t           data_type_id;
    uint64_t           signature;
    CanardTransferType type;        /* Broadcast; Request (we serve); Response (we called) */
    bool               high_priority;
    auvcan_handler_t   handler;
    void              *user;
} auvcan_sub_t;

struct auvcan {
    auvcan_config_t cfg;
    CanardInstance  canard;
    uint8_t         pool[AUVCAN_POOL_BYTES];

    auvcan_sub_t    subs[AUVCAN_MAX_SUBS];
    uint8_t         n_subs;
    bool            started;

    uint64_t        boot_us;
    uint64_t        poll_now_us;     /* single clock reading per auvcan_poll() */
    uint64_t        next_status_us;
    uint8_t         status_tid;

    uint8_t         app_health;      /* set by the board */
    uint8_t         app_mode;
    uint16_t        status_code;

    /* kill state */
    auvcan_fresh_t  kill_fresh;
    bool            kill_msg_armed;
    bool            killed;
    uint32_t        kill_events;

    /* restart */
    bool            reboot_pending;
    uint64_t        reboot_at_us;

    /* diagnostics */
    uint32_t        rx_errors;       /* canardHandleRxFrame errors other than "not for us" */
    uint32_t        tx_dropped;
    uint32_t        last_bus_off;
    uint32_t        last_rx_overflow;
};

/* ---- Lifecycle ----------------------------------------------------------- */
int  auvcan_init(auvcan_t *can, const auvcan_config_t *cfg);
int  auvcan_subscribe(auvcan_t *can, uint16_t data_type_id, uint64_t signature,
                      auvcan_handler_t handler, void *user);
int  auvcan_subscribe_hp(auvcan_t *can, uint16_t data_type_id, uint64_t signature,
                         auvcan_handler_t handler, void *user);   /* high-priority lane */
int  auvcan_serve(auvcan_t *can, uint8_t service_id, uint64_t signature,
                  auvcan_handler_t handler, void *user);
int  auvcan_on_response(auvcan_t *can, uint8_t service_id, uint64_t signature,
                        auvcan_handler_t handler, void *user);   /* responses to our requests */
int  auvcan_start(auvcan_t *can);       /* applies hardware filters; subscribe before this */
void auvcan_poll(auvcan_t *can);        /* call every main-loop iteration (>= 1 kHz ideal) */

/* ---- Sending ------------------------------------------------------------- */
int  auvcan_broadcast(auvcan_t *can, uint64_t signature, uint16_t data_type_id,
                      uint8_t *transfer_id, uint8_t priority,
                      const void *payload, uint16_t len);
int  auvcan_respond(auvcan_t *can, CanardRxTransfer *req, uint64_t signature,
                    const void *payload, uint16_t len);
int  auvcan_request(auvcan_t *can, uint8_t dest_node, uint64_t signature, uint8_t service_id,
                    uint8_t *transfer_id, uint8_t priority, const void *payload, uint16_t len);

/* ---- State --------------------------------------------------------------- */
/* Actuator nodes (kill_timeout_ms > 0): true at boot, true unless a fresh
 * cuauv.Kill says armed. Nodes with kill_timeout_ms == 0: always false. */
bool     auvcan_is_killed(const auvcan_t *can);
uint64_t auvcan_micros(const auvcan_t *can);
void     auvcan_set_health(auvcan_t *can, uint8_t health);   /* UAVCAN_PROTOCOL_NODESTATUS_HEALTH_* */
void     auvcan_set_mode(auvcan_t *can, uint8_t mode);       /* UAVCAN_PROTOCOL_NODESTATUS_MODE_* */
void     auvcan_set_status_code(auvcan_t *can, uint16_t code);
void     auvcan_get_bus_stats(const auvcan_t *can, auvcan_bus_stats_t *out);
uint16_t auvcan_pool_peak(auvcan_t *can);

#endif
