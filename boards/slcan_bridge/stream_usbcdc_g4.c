/*
 * stream_usbcdc_g4.c - the bridge's link to the host over the board's own USB port,
 * as a USB CDC virtual serial port. Selected with BOARD_BRIDGE_USB=1; the UART
 * version (stream_g4.c) is used otherwise. No USB-UART cable: the board plugs
 * straight into the computer and shows up as /dev/cu.usbmodem… (macOS) or
 * /dev/ttyACM… (Linux). The baud rate the host asks for is accepted and ignored.
 *
 *   python tools/bench.py monitor slcan:/dev/cu.usbmodemXXXX
 *
 * CubeMX (STM32G474):
 *   Connectivity > USB: Device (FS)                       PA11 = DM, PA12 = DP (fixed)
 *   Middleware > USB_DEVICE: Class = Communication Device Class (Virtual Port Com)
 *   Clock Configuration: USB clock mux = HSI48 (48 MHz)
 *   RCC > CRS SYNC = USB_FS                               (keeps HSI48 within USB's ±0.25 %)
 *   NVIC: "USB low priority interrupt" enabled (CubeMX does this with USB_DEVICE)
 * Nothing to edit in the generated USB files: stream_init() registers this file's
 * CDC callbacks in place of the ones CubeMX put in usbd_cdc_if.c.
 *
 * Hardware (USB-C 16-pin receptacle, USB 2.0, e.g. TYPE-C 16PIN 2MD(073)): see the
 * bridge README, "USB-C connector". In short: CC1 and CC2 each 5.1 kΩ to GND,
 * A6+B6 -> PA12 (D+), A7+B7 -> PA11 (D−), no external D+ pull-up, ESD on D+/D−/VBUS.
 *
 * Flow:
 *   RX: the USB ISR (Receive callback) copies each OUT packet (≤ 64 B) into a 1 KB
 *       ring. The endpoint is re-armed only while 64 B of room remain; otherwise
 *       poll() re-arms it later, so the host is held off (NAK) instead of losing data.
 *   TX: write() fills a 4 KB ring; poll() sends the next contiguous chunk (≤ 512 B)
 *       when the previous transfer has completed (TransmitCplt callback). A chunk is
 *       never an exact multiple of 64 B, so no zero-length packet is ever needed.
 */
#include "board_config.h"

#if BOARD_BRIDGE_USB

#include "stream.h"

#include <stdbool.h>
#include <string.h>

#include "main.h"
#include "usbd_cdc.h"

#ifdef USE_USBD_COMPOSITE
#error "stream_usbcdc_g4.c expects a single-class CDC device (USE_USBD_COMPOSITE is set)"
#endif

#define RX_SIZE     1024u                /* powers of two */
#define TX_SIZE     4096u
#define PKT         64u                  /* full-speed bulk packet */
#define CHUNK_MAX   512u

extern USBD_HandleTypeDef hUsbDeviceFS;  /* CubeMX: USB_Device/App/usb_device.c */

static uint8_t           s_rx[RX_SIZE];
static volatile uint32_t s_rx_head;      /* written by the USB ISR */
static uint32_t          s_rx_tail;      /* main loop */
static volatile bool     s_rx_paused;    /* OUT endpoint not re-armed: ring was full */
static uint8_t           s_rx_pkt[PKT];  /* USB writes each OUT packet here */

static uint8_t           s_tx[TX_SIZE];
static uint32_t          s_tx_head, s_tx_tail;
static volatile bool     s_tx_busy;      /* IN transfer in flight */
static uint8_t           s_tx_pkt[CHUNK_MAX];

static volatile uint32_t s_errors;       /* bytes dropped (TX ring full) */
static uint8_t           s_line_coding[7] = { 0x00, 0x10, 0x0E, 0x00, 0, 0, 8 };  /* 921600 8N1 */
static bridge_stream_t   s_stream;

static uint32_t rx_room(void) { return RX_SIZE - (s_rx_head - s_rx_tail); }

/* ---- CDC callbacks (called by the ST USB device library, mostly from the USB ISR) ---- */

static int8_t cdc_init(void)
{
    s_tx_busy = false;
    s_rx_paused = false;
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, s_tx_pkt, 0);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, s_rx_pkt);
    return (int8_t)USBD_OK;
}

static int8_t cdc_deinit(void)
{
    s_tx_busy = false;
    return (int8_t)USBD_OK;
}

static int8_t cdc_control(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    switch (cmd) {
    case CDC_SET_LINE_CODING:                        /* host picked a "baud rate": remember, ignore */
        if (length >= sizeof(s_line_coding)) memcpy(s_line_coding, pbuf, sizeof(s_line_coding));
        break;
    case CDC_GET_LINE_CODING:
        if (length >= sizeof(s_line_coding)) memcpy(pbuf, s_line_coding, sizeof(s_line_coding));
        break;
    default:                                         /* SET_CONTROL_LINE_STATE (DTR/RTS) etc.: nothing to do */
        break;
    }
    return (int8_t)USBD_OK;
}

static int8_t cdc_receive(uint8_t *buf, uint32_t *len)
{
    uint32_t n = *len;
    if (n > rx_room()) {                             /* can't happen while we re-arm only with room */
        s_errors += n - rx_room();
        n = rx_room();
    }
    for (uint32_t i = 0; i < n; i++) s_rx[(s_rx_head + i) & (RX_SIZE - 1u)] = buf[i];
    s_rx_head += n;

    if (rx_room() >= PKT) {
        USBD_CDC_SetRxBuffer(&hUsbDeviceFS, s_rx_pkt);
        USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    } else {
        s_rx_paused = true;                          /* poll() re-arms once the app has read */
    }
    return (int8_t)USBD_OK;
}

static int8_t cdc_tx_done(uint8_t *buf, uint32_t *len, uint8_t epnum)
{
    (void)buf; (void)len; (void)epnum;
    s_tx_busy = false;
    return (int8_t)USBD_OK;
}

static USBD_CDC_ItfTypeDef s_fops = {
    .Init         = cdc_init,
    .DeInit       = cdc_deinit,
    .Control      = cdc_control,
    .Receive      = cdc_receive,
    .TransmitCplt = cdc_tx_done,
};

/* ---- bridge_stream_t -------------------------------------------------------- */

static size_t u_read(void *ctx, uint8_t *buf, size_t max)
{
    (void)ctx;
    const uint32_t head = s_rx_head;
    size_t n = 0;
    while (s_rx_tail != head && n < max) {
        buf[n++] = s_rx[s_rx_tail & (RX_SIZE - 1u)];
        s_rx_tail++;
    }
    return n;
}

static size_t u_room(void *ctx)
{
    (void)ctx;
    return TX_SIZE - (s_tx_head - s_tx_tail) - 1u;
}

static void u_write(void *ctx, const char *s, size_t n)
{
    (void)ctx;
    if (n > u_room(NULL)) { s_errors += (uint32_t)n; return; }
    for (size_t i = 0; i < n; i++) s_tx[(s_tx_head + i) & (TX_SIZE - 1u)] = (uint8_t)s[i];
    s_tx_head += (uint32_t)n;
}

static void u_poll(void *ctx)
{
    (void)ctx;
    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
        s_tx_tail = s_tx_head;                       /* no host: drop, don't let replies pile up */
        return;
    }
    if (s_rx_paused && rx_room() >= PKT) {           /* the app caught up: accept data again */
        s_rx_paused = false;
        USBD_CDC_SetRxBuffer(&hUsbDeviceFS, s_rx_pkt);
        USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    }
    if (s_tx_busy || s_tx_head == s_tx_tail) return;

    uint32_t len = s_tx_head - s_tx_tail;
    if (len > CHUNK_MAX) len = CHUNK_MAX;
    if (len % PKT == 0u) len--;                      /* never a multiple of 64: no ZLP needed */
    for (uint32_t i = 0; i < len; i++) s_tx_pkt[i] = s_tx[(s_tx_tail + i) & (TX_SIZE - 1u)];

    s_tx_busy = true;
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, s_tx_pkt, len);
    if (USBD_CDC_TransmitPacket(&hUsbDeviceFS) == USBD_OK) {
        s_tx_tail += len;
    } else {
        s_tx_busy = false;                           /* busy or not ready: retry next poll */
    }
}

static uint32_t u_errors(void *ctx) { (void)ctx; return s_errors; }

const bridge_stream_t *stream_init(void)
{
    /* Replace CubeMX's CDC callbacks with ours. MX_USB_Device_Init() has run, but the
     * host configures the device only after enumeration (milliseconds later), which
     * is when cdc_init() is called. If that already happened, run it now. */
    if (USBD_CDC_RegisterInterface(&hUsbDeviceFS, &s_fops) != USBD_OK) return NULL;
    if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) {
        cdc_init();
        USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    }
    s_stream = (bridge_stream_t){ .ctx = NULL, .read = u_read, .room = u_room, .write = u_write,
                                  .poll = u_poll, .errors = u_errors };
    return &s_stream;
}

#endif /* BOARD_BRIDGE_USB */
