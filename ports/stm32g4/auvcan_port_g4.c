#include "auvcan_port_g4.h"

#include <string.h>

#include "auvcan_spec.h"
#include "auvcan_bittiming.h"

#define G4_EXT_FILTERS   8u      /* fixed message RAM: 8 extended filter elements */
#define MAX_INSTANCES    3u      /* FDCAN1..3 */

static auvcan_g4_t *s_instances[MAX_INSTANCES];

/* DLC macros changed encoding between HAL versions (plain 0..8 vs <<16);
 * going through the macros keeps us correct on both. */
static const uint32_t k_dlc[9] = {
    FDCAN_DLC_BYTES_0, FDCAN_DLC_BYTES_1, FDCAN_DLC_BYTES_2, FDCAN_DLC_BYTES_3,
    FDCAN_DLC_BYTES_4, FDCAN_DLC_BYTES_5, FDCAN_DLC_BYTES_6, FDCAN_DLC_BYTES_7,
    FDCAN_DLC_BYTES_8,
};

static auvcan_g4_t *lookup(const FDCAN_HandleTypeDef *h)
{
    for (uint32_t i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i] != NULL && s_instances[i]->hfdcan == h) {
            return s_instances[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* ISR side                                                                   */
/* ------------------------------------------------------------------------- */
/*
loops while the FIFO has frames:

    1. HAL_FDCAN_GetRxMessage() copies out header and data, which frees the hardware slot.
    2. It drops anything that isn’t a 29-bit data frame, and anything with an FD length.
    3. It converts to libcanard’s CanardCANFrame: ID plus the CANARD_CAN_FRAME_EFF flag, length and data.
    4. It pushes the frame into the ring: FIFO0 → rx_hp, FIFO1 → rx. If the ring is full, the frame is dropped and counted.
*/
static void drain_fifo(auvcan_g4_t *g4, uint32_t fifo, can_ring_t *ring)
{
    FDCAN_RxHeaderTypeDef hdr;
    uint8_t data[8];

    while (HAL_FDCAN_GetRxFifoFillLevel(g4->hfdcan, fifo) > 0u) {
        if (HAL_FDCAN_GetRxMessage(g4->hfdcan, fifo, &hdr, data) != HAL_OK) {
            break;
        }
        if (hdr.IdType != FDCAN_EXTENDED_ID || hdr.RxFrameType != FDCAN_DATA_FRAME) {
            continue;                                   /* DroneCAN is 29-bit data frames only */
        }
        uint8_t len = 0xFFu;
        for (uint8_t i = 0; i <= 8u; i++) {
            if (k_dlc[i] == hdr.DataLength) { len = i; break; }
        }
        if (len == 0xFFu) {
            continue;                                   /* FD length on a classic bus: drop */
        }
        CanardCANFrame f;
        memset(&f, 0, sizeof(f));
        f.id = (hdr.Identifier & CANARD_CAN_EXT_ID_MASK) | CANARD_CAN_FRAME_EFF;
        f.data_len = len;
        memcpy(f.data, data, len);
        (void)can_ring_push(ring, &f);                  /* full -> counted in ring->dropped */
    }
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t its)
{
    auvcan_g4_t *g4 = lookup(hfdcan);
    if (g4 == NULL) return;
    if (its & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) g4->hw_lost++;
    drain_fifo(g4, FDCAN_RX_FIFO0, &g4->rx_hp);
}

void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t its)
{
    auvcan_g4_t *g4 = lookup(hfdcan);
    if (g4 == NULL) return;
    if (its & FDCAN_IT_RX_FIFO1_MESSAGE_LOST) g4->hw_lost++;
    drain_fifo(g4, FDCAN_RX_FIFO1, &g4->rx);
}

/*
Handles bus-off. When the controller’s transmit error counter passes 255 it disconnects itself from the bus and sets INIT. The G4 never rejoins on its own. Clearing INIT starts the standard recovery: wait for 128 × 11 recessive bits (~1.4 ms at 1 Mbps), then rejoin. Each event increments bus_off_count, which turns NodeStatus WARNING for a second.
*/
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t its)
{
    auvcan_g4_t *g4 = lookup(hfdcan);
    if (g4 == NULL || (its & FDCAN_IT_BUS_OFF) == 0u) return;

    FDCAN_ProtocolStatusTypeDef ps;
    HAL_FDCAN_GetProtocolStatus(hfdcan, &ps);
    if (ps.BusOff != 0u) {
        g4->bus_off_count++;
        /* FDCAN parks itself in INIT on bus-off. Clearing INIT starts the
         * mandatory 128 x 11 recessive-bit wait, then it rejoins the bus. */
        CLEAR_BIT(hfdcan->Instance->CCCR, FDCAN_CCCR_INIT);
    }
}

/* ------------------------------------------------------------------------- */
/* Main-loop side (port vtable)                                               */
/* ------------------------------------------------------------------------- */

// filters and interrupts, then go on-bus
static int g4_start(void *ctx, const auvcan_filter_t *filters, uint8_t n)
{
    auvcan_g4_t *g4 = ctx;
    FDCAN_HandleTypeDef *h = g4->hfdcan;

    const uint8_t n_hw = (n > G4_EXT_FILTERS) ? (uint8_t)G4_EXT_FILTERS : n;
    for (uint8_t i = 0; i < n_hw; i++) {
        FDCAN_FilterTypeDef f = { //high-priority subs (Kill, any subscribe_hp) go to FIFO0, others go to FIFO1
            .IdType       = FDCAN_EXTENDED_ID,
            .FilterIndex  = i,
            .FilterType   = FDCAN_FILTER_MASK,
            .FilterConfig = filters[i].high_priority ? FDCAN_FILTER_TO_RXFIFO0_HP
                                                     : FDCAN_FILTER_TO_RXFIFO1,
            .FilterID1    = filters[i].id & CANARD_CAN_EXT_ID_MASK,
            .FilterID2    = filters[i].mask & CANARD_CAN_EXT_ID_MASK,
        };
        if (HAL_FDCAN_ConfigFilter(h, &f) != HAL_OK) return -2;
    }

    /* More subscriptions than filter slots: accept the rest in software. */
    const uint32_t non_matching_ext = (n > G4_EXT_FILTERS) ? FDCAN_ACCEPT_IN_RX_FIFO1 : FDCAN_REJECT;
    if (HAL_FDCAN_ConfigGlobalFilter(h, FDCAN_REJECT, non_matching_ext,
                                     FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) != HAL_OK) return -2;

    /* All interrupts stay on line 0 (the reset default), so ONE ISR is the only
     * producer for both rx_hp and rx. Do not route anything to line 1:
     * HAL_FDCAN_IRQHandler() services every pending flag no matter which line
     * fired, so a line-0 ISR preempting a line-1 ISR would drain the same FIFO
     * and push into the same ring from two contexts at once.
     * Kill still goes first: g4_rx() pops rx_hp before rx. */
    if (HAL_FDCAN_ActivateNotification(h,
            FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST |
            FDCAN_IT_RX_FIFO1_NEW_MESSAGE | FDCAN_IT_RX_FIFO1_MESSAGE_LOST |
            FDCAN_IT_BUS_OFF, 0u) != HAL_OK) return -2;

    return (HAL_FDCAN_Start(h) == HAL_OK) ? 0 : -2;
}

/*
Rejects non-extended frames (-1). If all 3 TX buffers are busy (TXFQS.TFQF) returns 0 (flush_tx retries later). Otherwise builds an ST TX header (classic, no bit-rate switch) and calls HAL_FDCAN_AddMessageToTxFifoQ() → 1.
*/
static int g4_tx(void *ctx, const CanardCANFrame *frame)
{
    auvcan_g4_t *g4 = ctx;
    if ((frame->id & CANARD_CAN_FRAME_EFF) == 0u || frame->data_len > 8u) {
        return -1;
    }
    /* Queue mode: TXFQS.TFFL (what HAL_FDCAN_GetTxFifoFreeLevel returns) always
     * reads 0 when TXBC.TFQM = 1 (RM0440), so check the queue-full flag instead. */
    if ((g4->hfdcan->Instance->TXFQS & FDCAN_TXFQS_TFQF) != 0u) {
        return 0;
    }
    FDCAN_TxHeaderTypeDef hdr = {
        .Identifier          = frame->id & CANARD_CAN_EXT_ID_MASK,
        .IdType              = FDCAN_EXTENDED_ID,
        .TxFrameType         = FDCAN_DATA_FRAME,
        .DataLength          = k_dlc[frame->data_len],
        .ErrorStateIndicator = FDCAN_ESI_ACTIVE,
        .BitRateSwitch       = FDCAN_BRS_OFF,
        .FDFormat            = FDCAN_CLASSIC_CAN,
        .TxEventFifoControl  = FDCAN_NO_TX_EVENTS,
        .MessageMarker       = 0u,
    };
    return (HAL_FDCAN_AddMessageToTxFifoQ(g4->hfdcan, &hdr, frame->data) == HAL_OK) ? 1 : 0;
}

/*
Pops rx_hp first, then rx. That’s the priority lane: a queued Kill is always processed before queued telemetry.
*/
static bool g4_rx(void *ctx, CanardCANFrame *frame)
{
    auvcan_g4_t *g4 = ctx;
    return can_ring_pop(&g4->rx_hp, frame) || can_ring_pop(&g4->rx, frame);   /* HP lane first */
}

/*
HAL_GetTick() (1 ms SysTick) × 1000. The 32-bit tick wraps after 49 days; adding the difference since the last call to a 64-bit counter makes the wrap harmless. 1 ms resolution is enough for ≥200 ms watchdogs.
*/
static uint64_t g4_micros(void *ctx)
{
    auvcan_g4_t *g4 = ctx;
    const uint32_t now = HAL_GetTick();
    g4->tick_ext_ms += (uint32_t)(now - g4->last_tick_ms);   /* wrap-safe */
    g4->last_tick_ms = now;
    return g4->tick_ext_ms * 1000u;
}

/*
Copies the 12-byte factory UID from UID_BASE (0x1FFF7590), zero-padded to 16. Every chip’s UID is different, so the GUI Tool can tell two physical boards apart.
*/
static void g4_unique_id(void *ctx, uint8_t out[16])
{
    (void)ctx;
    memset(out, 0, 16);
    memcpy(out, (const void *)UID_BASE, 12);   /* 96-bit factory UID */
}

/*
Live TEC/REC and error-passive from the controller, plus our counters (bus-offs, hardware + ring overflows).
*/
static void g4_get_stats(void *ctx, auvcan_bus_stats_t *out)
{
    auvcan_g4_t *g4 = ctx;
    FDCAN_ErrorCountersTypeDef ec;
    FDCAN_ProtocolStatusTypeDef ps;
    HAL_FDCAN_GetErrorCounters(g4->hfdcan, &ec);
    HAL_FDCAN_GetProtocolStatus(g4->hfdcan, &ps);

    out->tec = (uint8_t)ec.TxErrorCnt;
    out->rec = (uint8_t)ec.RxErrorCnt;
    out->error_passive = (ps.ErrorPassive != 0u);
    out->bus_off_count = g4->bus_off_count;
    out->rx_overflow = g4->hw_lost + g4->rx_hp.dropped + g4->rx.dropped;
    out->tx_errors = 0u;
}

static void g4_reboot(void *ctx)
{
    (void)ctx;
    NVIC_SystemReset();
}

/* ------- Peripheral Configuration ------------------------------------------ */

int auvcan_g4_init(auvcan_g4_t *g4, FDCAN_HandleTypeDef *h,
                   FDCAN_GlobalTypeDef *instance, uint32_t kernel_clock_hz)
{
    auvcan_bit_timing_t bt;
    if (auvcan_compute_bit_timing(kernel_clock_hz, AUVCAN_BITRATE,
                                  AUVCAN_SAMPLE_POINT_PERMILLE, &bt) != 0) {
        return -1;
    }

    memset(g4, 0, sizeof(*g4));
    g4->hfdcan = h;
    g4->kernel_clock_hz = kernel_clock_hz;
    can_ring_init(&g4->rx_hp);
    can_ring_init(&g4->rx);
    g4->last_tick_ms = HAL_GetTick();

    h->Instance = instance;
    h->Init.ClockDivider         = FDCAN_CLOCK_DIV1;
    h->Init.FrameFormat          = FDCAN_FRAME_CLASSIC;
    h->Init.Mode                 = FDCAN_MODE_NORMAL;
    h->Init.AutoRetransmission   = ENABLE;
    h->Init.TransmitPause        = DISABLE;
    h->Init.ProtocolException    = DISABLE;
    h->Init.NominalPrescaler     = bt.prescaler;
    h->Init.NominalSyncJumpWidth = bt.sjw;
    h->Init.NominalTimeSeg1      = bt.seg1;
    h->Init.NominalTimeSeg2      = bt.seg2;
    h->Init.DataPrescaler        = 1u;      /* unused in classic mode, but must be valid */
    h->Init.DataSyncJumpWidth    = 1u;
    h->Init.DataTimeSeg1         = 1u;
    h->Init.DataTimeSeg2         = 1u;
    h->Init.StdFiltersNbr        = 0u;
    h->Init.ExtFiltersNbr        = G4_EXT_FILTERS;
    h->Init.TxFifoQueueMode      = FDCAN_TX_QUEUE_OPERATION;   /* lowest ID (highest prio) first */

    for (uint32_t i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i] == NULL || s_instances[i] == g4) {
            s_instances[i] = g4;
            break;
        }
    }

    if (HAL_FDCAN_Init(h) != HAL_OK) {
        return -2;
    }

    g4->port = (auvcan_port_t){
        .ctx = g4, .start = g4_start, .tx = g4_tx, .rx = g4_rx, .micros = g4_micros,
        .unique_id = g4_unique_id, .get_stats = g4_get_stats, .reboot = g4_reboot,
    };
    return 0;
}

const auvcan_port_t *auvcan_g4_port(auvcan_g4_t *g4)
{
    return &g4->port;
}
