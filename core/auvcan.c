#include "auvcan.h"

#include <string.h>

#include <uavcan.protocol.NodeStatus.h>
#include <uavcan.protocol.GetNodeInfo.h>
#include <uavcan.protocol.GetNodeInfo_req.h>
#include <uavcan.protocol.GetNodeInfo_res.h>
#include <uavcan.protocol.RestartNode.h>
#include <uavcan.protocol.RestartNode_req.h>
#include <uavcan.protocol.RestartNode_res.h>
#include <cuauv.Kill.h>

#define REBOOT_DELAY_US      100000u   /* let the RestartNode response leave the TX queue */

/*
DroneCAN 29-bit ID
Message frame: priority[28:24] | type ID[23:8] | service=0 [7] | source node[6:0]
Service frame: priority | service ID[23:16] | req/resp [15] | destination node[14:8] | service=1 [7] | source
*/
#define DTID_SHIFT           8u
#define SERVICE_BIT          (1u << 7)
#define MSG_FILTER_MASK      ((0xFFFFu << DTID_SHIFT) | SERVICE_BIT)
#define SERVICE_FILTER_MASK  ((0x7Fu << DTID_SHIFT) | SERVICE_BIT)

/* ------------------------------------------------------------------------- */
/* Internal helper   */
/* ------------------------------------------------------------------------- */

static auvcan_t *self_of(const CanardInstance *ins)
{
    return (auvcan_t *)canardGetUserReference((CanardInstance *)ins);
}

/*
Looks up an entry
*/
static const auvcan_sub_t *find_sub(const auvcan_t *can, CanardTransferType type, uint16_t id)
{
    for (uint8_t i = 0; i < can->n_subs; i++) {
        if (can->subs[i].type == type && can->subs[i].data_type_id == id) {
            return &can->subs[i];
        }
    }
    return NULL;
}

/*
Register an entry
refuse in 4 cases:
- already started (-1)
- table full (-1)
- no handler (-1): a NULL function pointer
- duplicate (-2): the same (type, ID) pair is already registered
*/
static int add_sub(auvcan_t *can, CanardTransferType type, uint16_t id, uint64_t sig,
                   bool hp, auvcan_handler_t handler, void *user)
{
    if (can->started || can->n_subs >= AUVCAN_MAX_SUBS || handler == NULL) {
        return -1;
    }
    if (find_sub(can, type, id) != NULL) {
        return -2;   /* one handler per type; dispatch inside it if you need fan-out */
    }
    can->subs[can->n_subs++] = (auvcan_sub_t){
        .data_type_id = id, .signature = sig, .type = type,
        .high_priority = hp, .handler = handler, .user = user,
    };
    return 0;
}

/*
Move frames from libcanard's software TX queue into the CAN's hardware TX buffers
untill hardware is full / empty queue
port->tx() returns:
    - 1 Frame is in a hardware buffer
    - 0 3 hardware buffers full
    - -1 safeguard - portw rejected it permanently
*/
static void flush_tx(auvcan_t *can)
{
    const CanardCANFrame *f;
    while ((f = canardPeekTxQueue(&can->canard)) != NULL) {
        const int r = can->cfg.port->tx(can->cfg.port->ctx, f); //tries to hand to the hardware
        if (r == 0) {
            break; //hardware full
        }
        if (r < 0) {
            can->tx_dropped++;
        }
        canardPopTxQueue(&can->canard);
    }
}

/*
Detect node state changes to killed and notify the board
*/
static void set_killed(auvcan_t *can, bool killed)
{
    if (killed == can->killed) {
        return;
    }
    can->killed = killed; //store killed state
    if (killed) {
        can->kill_events++; //count kills -- to keep track of glitches and stuff
    }
    if (can->cfg.on_kill_change != NULL) { //notify kill-related board (BMB and thruster?)
        can->cfg.on_kill_change(can, killed, can->cfg.kill_user);
    }
}

/*
Detect kill signal / conditions
- triggered by kill message (obviously)
- not hearing back for 250 ms
*/
static void evaluate_kill(auvcan_t *can, uint64_t now)
{
    if (can->cfg.kill_timeout_ms == 0u) {
        return;
    }
    const bool fresh = auvcan_fresh_ok(&can->kill_fresh, now);
    set_killed(can, !(fresh && can->kill_msg_armed));
}

/* ------------------------------------------------------------------------- */
/* Built-in handlers                                                          */
/* ------------------------------------------------------------------------- */

/*
Builds the 7-byte NodeStatus struct
    - uptime_sec - 4 byte - (now − boot_us) / 1e6
    - health - 2 bits 
    - mode - 3 bits  - from auvcan_set_mode()
    - sub_mode - 3 bits
    - vendor_specific_status_code - 2 bytes - (free for board to use )
*/
static void fill_node_status(auvcan_t *can, struct uavcan_protocol_NodeStatus *ns, uint64_t now)
{
    auvcan_bus_stats_t st;
    auvcan_get_bus_stats(can, &st);

    //health logic (ONLY BE RAISED BY CORE)
    uint8_t health = can->app_health;
    if (st.error_passive || st.bus_off_count != can->last_bus_off ||
        st.rx_overflow != can->last_rx_overflow) {
        if (health < UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING) { //stays error passive
            health = UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING;
        }
    }
    ns->uptime_sec = (uint32_t)((now - can->boot_us) / 1000000u);
    ns->health = health;
    ns->mode = can->app_mode;
    ns->sub_mode = 0;
    ns->vendor_specific_status_code = can->status_code;
}

/*
Triggered by uavcan.protocol.GetNodeInfo request
Respond: everything that identifies the node
*/
static void on_get_node_info(auvcan_t *can, CanardRxTransfer *t, void *user)
{
    (void)user;
    /* static: the response struct is ~640 B, too big for an MCU stack */
    static struct uavcan_protocol_GetNodeInfoResponse rsp;
    static uint8_t buf[UAVCAN_PROTOCOL_GETNODEINFO_RESPONSE_MAX_SIZE];

    memset(&rsp, 0, sizeof(rsp)); //clears last time's content
    fill_node_status(can, &rsp.status, auvcan_micros(can));
    rsp.software_version.major = can->cfg.sw_major;
    rsp.software_version.minor = can->cfg.sw_minor;
    rsp.software_version.optional_field_flags =
        (can->cfg.sw_vcs_commit != 0u) ? UAVCAN_PROTOCOL_SOFTWAREVERSION_OPTIONAL_FIELD_FLAG_VCS_COMMIT : 0u;
    rsp.software_version.vcs_commit = can->cfg.sw_vcs_commit;
    rsp.hardware_version.major = can->cfg.hw_major;
    rsp.hardware_version.minor = can->cfg.hw_minor;
    can->cfg.port->unique_id(can->cfg.port->ctx, rsp.hardware_version.unique_id);

    size_t n = 0;                                /* bounded strlen; strnlen isn't C11 */
    while (n < sizeof(rsp.name.data) && can->cfg.name[n] != '\0') {
        n++;
    }
    memcpy(rsp.name.data, can->cfg.name, n); //copy board name
    rsp.name.len = (uint8_t)n;

    //serialize into bits
    const uint32_t len = uavcan_protocol_GetNodeInfoResponse_encode(&rsp, buf); 
    auvcan_respond(can, t, UAVCAN_PROTOCOL_GETNODEINFO_SIGNATURE, buf, (uint16_t)len);
}

/*
Triggered by uavcan.protocol.RestartNode
    - magic number: 0xACCE551B1E DroneCAN standard
    - always respond: ok=false => refused to restart
*/
static void on_restart_node(auvcan_t *can, CanardRxTransfer *t, void *user)
{
    (void)user;
    struct uavcan_protocol_RestartNodeRequest req;
    struct uavcan_protocol_RestartNodeResponse rsp = { .ok = false };
    uint8_t buf[UAVCAN_PROTOCOL_RESTARTNODE_RESPONSE_MAX_SIZE];

    if (!uavcan_protocol_RestartNodeRequest_decode(t, &req) &&
        req.magic_number == UAVCAN_PROTOCOL_RESTARTNODE_REQUEST_MAGIC_NUMBER) {
        rsp.ok = true;
        can->reboot_pending = true;
        can->reboot_at_us = auvcan_micros(can) + REBOOT_DELAY_US;
    }
    const uint32_t len = uavcan_protocol_RestartNodeResponse_encode(&rsp, buf);
    auvcan_respond(can, t, UAVCAN_PROTOCOL_RESTARTNODE_SIGNATURE, buf, (uint16_t)len);
}

/*
Triggered by cuauv.Kill broadcast
*/
static void on_kill(auvcan_t *can, CanardRxTransfer *t, void *user)
{
    (void)user;
    struct cuauv_Kill msg;
    if (cuauv_Kill_decode(t, &msg)) {
        return;                                    //malformed: ignore, watchdog will catch silence
    }
    if (t->source_node_id != AUVCAN_NODE_CONTROLLER) { //wrong standard: ignore
        return;                                   
    }
    const uint64_t now = can->poll_now_us;        //clock reading the watchdog uses
    if (auvcan_fresh_feed_seq(&can->kill_fresh, now, msg.seq)) { //only react to fresh kill signals
        can->kill_msg_armed = msg.armed;
    }
    evaluate_kill(can, now);                      // act on a kill edge immediately
}

/* ------------------------------------------------------------------------- */
/* libcanard callbacks                                                        */
/* ------------------------------------------------------------------------- */

/*
libcanard's gatekeeper callback
*/
static bool should_accept(const CanardInstance *ins, uint64_t *out_sig, uint16_t id,
                          CanardTransferType type, uint8_t source_node_id)
{
    (void)source_node_id;
    const auvcan_sub_t *s = find_sub(self_of(ins), type, id);
    if (s == NULL) {
        return false;
    }
    *out_sig = s->signature; //CRC seeded with the data type's 64-bit signature
    return true;
}

static void on_reception(CanardInstance *ins, CanardRxTransfer *t)
{
    auvcan_t *can = self_of(ins);
    const auvcan_sub_t *s = find_sub(can, (CanardTransferType)t->transfer_type, t->data_type_id);
    if (s != NULL) {
        s->handler(can, t, s->user);
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                 */
/* ------------------------------------------------------------------------- */

/*
Setup: turns  zeroed auvcan_t into a working node (not yet on bus)
    1. validate the config (port, name, id), return -1 if not
    2. copy cfg into can->cfg, turn board struct into local var
    3. initialize libcanard, set can as user interface, set node ID
    4. Records the boot time, request first NodeStatus immediately
    5. Boots killed if kill_timeout_ms > 0
    6. Registers the built-ins (get_node_info, on_restart_node, on_kill)
*/
int auvcan_init(auvcan_t *can, const auvcan_config_t *cfg)
{
    if (cfg == NULL || cfg->port == NULL || cfg->name == NULL ||
        cfg->node_id < CANARD_MIN_NODE_ID || cfg->node_id > CANARD_MAX_NODE_ID) {
        return -1;
    }
    memset(can, 0, sizeof(*can));
    can->cfg = *cfg;

    canardInit(&can->canard, can->pool, sizeof(can->pool), on_reception, should_accept, can);
    canardSetLocalNodeID(&can->canard, cfg->node_id);

    can->boot_us = auvcan_micros(can);
    can->poll_now_us = can->boot_us;
    can->next_status_us = can->boot_us;          /* first NodeStatus on the first poll */
    can->app_health = UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK;
    can->app_mode = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;

    /* Fail safe from the first instruction: actuators boot killed. */
    can->killed = (cfg->kill_timeout_ms != 0u);
    auvcan_fresh_init(&can->kill_fresh, cfg->kill_timeout_ms);

    add_sub(can, CanardTransferTypeRequest, UAVCAN_PROTOCOL_GETNODEINFO_ID,
            UAVCAN_PROTOCOL_GETNODEINFO_SIGNATURE, false, on_get_node_info, NULL);
    add_sub(can, CanardTransferTypeRequest, UAVCAN_PROTOCOL_RESTARTNODE_ID,
            UAVCAN_PROTOCOL_RESTARTNODE_SIGNATURE, false, on_restart_node, NULL);
    if (cfg->kill_timeout_ms != 0u) {
        add_sub(can, CanardTransferTypeBroadcast, CUAUV_KILL_ID,
                CUAUV_KILL_SIGNATURE, true, on_kill, NULL);
    }
    return 0;
}

/*
Broadcast messages, normal lane
*/
int auvcan_subscribe(auvcan_t *can, uint16_t id, uint64_t sig, auvcan_handler_t h, void *user)
{
    return add_sub(can, CanardTransferTypeBroadcast, id, sig, false, h, user);
}

/*
Broadcast messages that are safety- or latency-critical (high priority)
*/
int auvcan_subscribe_hp(auvcan_t *can, uint16_t id, uint64_t sig, auvcan_handler_t h, void *user)
{
    return add_sub(can, CanardTransferTypeBroadcast, id, sig, true, h, user);
}

/*
Services this node answers
*/
int auvcan_serve(auvcan_t *can, uint8_t service_id, uint64_t sig, auvcan_handler_t h, void *user)
{
    return add_sub(can, CanardTransferTypeRequest, service_id, sig, false, h, user);
}

/*
Responses to requests this node sent
*/
int auvcan_on_response(auvcan_t *can, uint8_t service_id, uint64_t sig, auvcan_handler_t h, void *user)
{
    return add_sub(can, CanardTransferTypeResponse, service_id, sig, false, h, user);
}

/*
Converts the subscription table into hardware filters and puts the node on the bus
    -return: 0 success, others on error
*/
int auvcan_start(auvcan_t *can)
{
    auvcan_filter_t f[AUVCAN_MAX_SUBS + 1];
    uint8_t n = 0;

    //One filter for every service request/response addressed to this node.
    f[n++] = (auvcan_filter_t){
        .id = ((uint32_t)can->cfg.node_id << DTID_SHIFT) | SERVICE_BIT,
        .mask = SERVICE_FILTER_MASK,
        .high_priority = false,
    };
    //One filter for high-priority / broadcasts first
    for (int pass = 0; pass < 2; pass++) {
        for (uint8_t i = 0; i < can->n_subs; i++) {
            const auvcan_sub_t *s = &can->subs[i];
            if (s->type != CanardTransferTypeBroadcast || s->high_priority != (pass == 0)) {
                continue;
            }
            f[n++] = (auvcan_filter_t){
                .id = (uint32_t)s->data_type_id << DTID_SHIFT,
                .mask = MSG_FILTER_MASK,
                .high_priority = s->high_priority,
            };
        }
    }
    const int r = can->cfg.port->start(can->cfg.port->ctx, f, n);
    if (r == 0) {
        can->started = true;
    }
    return r;
}

/*
Called every main-loop iteration
    1. Read the clock once
    2. Drain the RX rings -> libcanard -> handlers
    3. Evaluate Kill watchdog for silences / kill messages
    4. 1 Hz Housekeeping node status
    5. Flush TX
    6. Perform pending restart
*/
void auvcan_poll(auvcan_t *can)
{
    const uint64_t now = auvcan_micros(can);
    can->poll_now_us = now;          

    // 1. RX: drain the ring the ISR filled. 
    CanardCANFrame frame;
    while (can->cfg.port->rx(can->cfg.port->ctx, &frame)) {
        const int16_t r = canardHandleRxFrame(&can->canard, &frame, now);
        if (r < 0 && r != -CANARD_ERROR_RX_NOT_WANTED && r != -CANARD_ERROR_RX_WRONG_ADDRESS) {
            can->rx_errors++;
        }
    }

    // 2. Kill watchdog: catches silence, not just explicit kill messages.
    evaluate_kill(can, now);

    // 3. 1 Hz NodeStatus polling
    if (now >= can->next_status_us) {
        can->next_status_us += (uint64_t)AUVCAN_NODESTATUS_PERIOD_MS * 1000u;
        if (now >= can->next_status_us) {
            can->next_status_us = now + (uint64_t)AUVCAN_NODESTATUS_PERIOD_MS * 1000u;
        }
        struct uavcan_protocol_NodeStatus ns;
        uint8_t buf[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];
        fill_node_status(can, &ns, now);
        const uint32_t len = uavcan_protocol_NodeStatus_encode(&ns, buf);
        canardBroadcast(&can->canard, UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                        UAVCAN_PROTOCOL_NODESTATUS_ID, &can->status_tid,
                        AUVCAN_PRIO_NODESTATUS, buf, (uint16_t)len);

        auvcan_bus_stats_t st;
        auvcan_get_bus_stats(can, &st);
        can->last_bus_off = st.bus_off_count;      /* WARNING lasts one status period per event */
        can->last_rx_overflow = st.rx_overflow;

        canardCleanupStaleTransfers(&can->canard, now);
    }

    // 4. TX: move queued frames into the hardware.
    flush_tx(can);

    // 5. Deferred restart, after the response has had time to leave.
    if (can->reboot_pending && now >= can->reboot_at_us) {
        can->cfg.port->reboot(can->cfg.port->ctx);
        can->reboot_pending = false;             /* only reached in simulation */
    }
}

/*
Publish a message to everyone.
    - tid: 5-bit transfer ID that libcanard increments on every send
        - must be a separate persistent variable for each message type a node publishes
    - prio: one of the AUVCAN_PRIO_* constants
*/
int auvcan_broadcast(auvcan_t *can, uint64_t sig, uint16_t id, uint8_t *tid, uint8_t prio,
                     const void *payload, uint16_t len)
{
    const int16_t r = canardBroadcast(&can->canard, sig, id, tid, prio, payload, len);
    flush_tx(can);                               //flushed immediately
    return r;
}

/*
Reply to a request from inside a serve handler
    - send response to req->source_node_id
    - Reuses the request's transfer ID and priority (required by DroneCAN)
    - must be callsed inside a handler
*/
int auvcan_respond(auvcan_t *can, CanardRxTransfer *req, uint64_t sig,
                   const void *payload, uint16_t len)
{
    const int16_t r = canardRequestOrRespond(&can->canard, req->source_node_id, sig,
                                             (uint8_t)req->data_type_id, &req->transfer_id,
                                             req->priority, CanardResponse, payload, len);
    flush_tx(can);
    return r;
}

/*
Send a request to one node
*/
int auvcan_request(auvcan_t *can, uint8_t dest, uint64_t sig, uint8_t service_id,
                   uint8_t *tid, uint8_t prio, const void *payload, uint16_t len)
{
    static const uint8_t empty[1];
    if (payload == NULL) {
        payload = empty;          /* libcanard memcpy()s even zero-length payloads */
        len = 0;
    }
    const int16_t r = canardRequestOrRespond(&can->canard, dest, sig, service_id, tid, prio,
                                             CanardRequest, payload, len);
    flush_tx(can);
    return r;
}

bool auvcan_is_killed(const auvcan_t *can)
{
    return can->killed;
}

uint64_t auvcan_micros(const auvcan_t *can)
{
    return can->cfg.port->micros(can->cfg.port->ctx);
}

void auvcan_set_health(auvcan_t *can, uint8_t health)      { can->app_health = health; }
void auvcan_set_mode(auvcan_t *can, uint8_t mode)          { can->app_mode = mode; }
void auvcan_set_status_code(auvcan_t *can, uint16_t code)  { can->status_code = code; }

void auvcan_get_bus_stats(const auvcan_t *can, auvcan_bus_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    if (can->cfg.port->get_stats != NULL) {
        can->cfg.port->get_stats(can->cfg.port->ctx, out);
    }
    out->tx_errors += can->tx_dropped;
}

uint16_t auvcan_pool_peak(auvcan_t *can)
{
    return canardGetPoolAllocatorStatistics(&can->canard).peak_usage_blocks;
}
