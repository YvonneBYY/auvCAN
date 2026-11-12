/*
 * test_usbcdc.c - the bridge's USB CDC link (boards/slcan_bridge/stream_usbcdc_g4.c)
 * against a fake ST USB device stack: callbacks registered, OUT packets into the
 * read ring, flow control when the ring is full, IN chunks that are never a
 * multiple of 64 bytes, one IN transfer at a time, and data dropped with no host.
 */
#include <stdio.h>
#include <string.h>
#include "usbd_cdc.h"
#include "stream.h"

USBD_HandleTypeDef hUsbDeviceFS;
static USBD_CDC_ItfTypeDef *fops;
static uint8_t *rx_buf, *tx_buf;
static uint32_t tx_len, n_rearm, n_tx;
static int tx_inflight;
static uint8_t host_got[16384];
static uint32_t host_n;
static uint8_t in_flight_copy[1024];      /* what USB was given; must not change until done */
static int corrupted;

uint8_t USBD_CDC_RegisterInterface(USBD_HandleTypeDef *p, USBD_CDC_ItfTypeDef *f) { (void)p; fops = f; return USBD_OK; }
uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *p, uint8_t *b, uint32_t n) { (void)p; tx_buf = b; tx_len = n; return USBD_OK; }
uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *p, uint8_t *b) { (void)p; rx_buf = b; return USBD_OK; }
uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *p) { (void)p; n_rearm++; return USBD_OK; }
uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *p)
{
    (void)p;
    if (tx_inflight) return USBD_BUSY;
    tx_inflight = 1; n_tx++;
    memcpy(in_flight_copy, tx_buf, tx_len);
    return USBD_OK;
}
static void host_ack_in(void)                /* the IN transfer completes: the host gets the bytes now */
{
    if (memcmp(in_flight_copy, tx_buf, tx_len) != 0) corrupted++;  /* buffer changed mid-transfer */
    memcpy(host_got + host_n, in_flight_copy, tx_len); host_n += tx_len;
    tx_inflight = 0; uint32_t l = tx_len; fops->TransmitCplt(tx_buf, &l, 0x81);
}
static void host_send(const char *s, uint32_t n) { memcpy(rx_buf, s, n); fops->Receive(rx_buf, &n); }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    const bridge_stream_t *s = stream_init();
    CHECK(s != NULL && fops != NULL, "stream_init registers CDC callbacks");

    /* host enumerates + configures */
    hUsbDeviceFS.dev_state = USBD_STATE_CONFIGURED;
    CHECK(fops->Init() == USBD_OK && rx_buf != NULL, "Init sets an RX buffer");

    /* line coding: any baud accepted, read back */
    uint8_t lc[7] = { 0x00, 0xC2, 0x01, 0x00, 0, 0, 8 };         /* 115200 */
    fops->Control(CDC_SET_LINE_CODING, lc, 7);
    uint8_t back[7] = {0};
    fops->Control(CDC_GET_LINE_CODING, back, 7);
    CHECK(memcmp(lc, back, 7) == 0, "line coding round-trips");
    CHECK(fops->Control(CDC_SET_CONTROL_LINE_STATE, NULL, 0) == USBD_OK, "DTR/RTS accepted");

    /* OUT packets arrive in order and are re-armed while there's room */
    uint32_t r0 = n_rearm;
    host_send("S8\r", 3); host_send("O\r", 2);
    uint8_t buf[2048]; size_t n = s->read(NULL, buf, sizeof buf);
    CHECK(n == 5 && memcmp(buf, "S8\rO\r", 5) == 0, "read gets host bytes in order (%zu)", n);
    CHECK(n_rearm == r0 + 2, "endpoint re-armed after each packet");

    /* flow control: fill the 1 KB ring with 64 B packets without reading */
    char pkt[64]; memset(pkt, 'x', sizeof pkt);
    r0 = n_rearm;
    for (int i = 0; i < 16; i++) host_send(pkt, 64);              /* exactly 1024 B */
    CHECK(n_rearm == r0 + 15, "stops re-arming when < 64 B room (rearms %u)", (unsigned)(n_rearm - r0));
    s->poll(NULL);
    CHECK(n_rearm == r0 + 15, "poll doesn't re-arm while still full");
    n = s->read(NULL, buf, 100);
    s->poll(NULL);
    CHECK(n == 100 && n_rearm == r0 + 16, "poll re-arms once the app has read");
    while (s->read(NULL, buf, sizeof buf) > 0) {}

    /* TX: chunks <= 512 B, never a multiple of 64, one in flight at a time */
    char big[3000]; for (int i = 0; i < 3000; i++) big[i] = (char)('A' + i % 26);
    s->write(NULL, big, sizeof big);
    int bad_len = 0, polls = 0;
    host_n = 0;
    while (host_n < sizeof big && polls < 100) {
        s->poll(NULL);
        if (tx_inflight) {
            if (tx_len > 512 || tx_len % 64 == 0) bad_len++;
            s->poll(NULL);                                       /* must not start a second transfer */
            host_ack_in();
        }
        polls++;
    }
    CHECK(host_n == sizeof big && memcmp(host_got, big, sizeof big) == 0, "host receives all 3000 B in order (%u)", (unsigned)host_n);
    CHECK(bad_len == 0, "no chunk > 512 B or a multiple of 64 B");
    CHECK(corrupted == 0, "packet buffer never changed while a transfer was in flight (%d)", corrupted);

    /* exactly 64 B queued: sent as 63 + 1 */
    host_n = 0; s->write(NULL, big, 64);
    s->poll(NULL); CHECK(tx_len == 63, "64 B goes out as 63 first (%u)", (unsigned)tx_len); host_ack_in();
    s->poll(NULL); CHECK(tx_len == 1, "then 1"); host_ack_in();

    /* TX ring full: write drops and counts */
    uint32_t e0 = s->errors(NULL);
    size_t room = s->room(NULL);
    s->write(NULL, big, room + 1);
    CHECK(s->errors(NULL) > e0, "write beyond room is dropped and counted");

    /* host gone: queued replies are discarded, not piled up */
    hUsbDeviceFS.dev_state = USBD_STATE_DEFAULT;
    tx_inflight = 0;
    s->write(NULL, "T1\r", 3);
    uint32_t tx0 = n_tx; s->poll(NULL);
    CHECK(n_tx == tx0 && s->room(NULL) == 4095, "no host: TX dropped, ring emptied");

    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
