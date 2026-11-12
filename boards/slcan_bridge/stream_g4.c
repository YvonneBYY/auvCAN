/*
 * stream_g4.c - the bridge's serial link on an STM32G4 UART.
 *
 * RX: circular DMA into a 512-byte buffer; read() follows the DMA write position,
 *     so no byte is lost while the main loop is busy (up to 512 bytes = ~5 ms at 921600).
 * TX: a 4 KB ring; poll() starts a DMA transfer of the next contiguous chunk whenever
 *     the previous one has finished.
 *
 * CubeMX (USART named by BOARD_BRIDGE_UART, see board_config.h):
 *   Mode Asynchronous, 8N1 · DMA: USARTx_RX = Circular, USARTx_TX = Normal ·
 *   NVIC: USARTx global interrupt + both DMA channel interrupts enabled.
 * This file owns HAL_UART_TxCpltCallback and HAL_UART_ErrorCallback.
 */
#include "board_config.h"

#if !BOARD_BRIDGE_USB                   /* BOARD_BRIDGE_USB=1 uses stream_usbcdc_g4.c instead */

#include "stream.h"

#include <string.h>

#include "main.h"                       /* CubeMX: HAL + UART handles */

#if !defined(HAL_UART_MODULE_ENABLED) || !defined(HAL_DMA_MODULE_ENABLED)
#error "No UART/DMA enabled in CubeMX: set BOARD_BRIDGE_UART to Asynchronous with DMA (RX circular, TX normal) and regenerate"
#endif

#define RX_SIZE 512u
#define TX_SIZE 4096u                   /* power of two */

extern UART_HandleTypeDef BOARD_BRIDGE_UART;

static uint8_t           s_rx[RX_SIZE];
static uint32_t          s_rx_rd;
static uint8_t           s_tx[TX_SIZE];
static uint32_t          s_tx_head;               /* written by the main loop */
static volatile uint32_t s_tx_tail;               /* advanced by the TX-complete ISR */
static volatile uint32_t s_tx_len;                /* bytes in flight, 0 = idle */
static volatile uint32_t s_errors;
static bridge_stream_t   s_stream;

static void start_rx(void)
{
    s_rx_rd = 0;
    (void)HAL_UART_Receive_DMA(&BOARD_BRIDGE_UART, s_rx, RX_SIZE);
}

static size_t g_read(void *ctx, uint8_t *buf, size_t max)
{
    (void)ctx;
    const uint32_t wr = RX_SIZE - __HAL_DMA_GET_COUNTER(BOARD_BRIDGE_UART.hdmarx);
    size_t n = 0;
    while (s_rx_rd != (wr % RX_SIZE) && n < max) {
        buf[n++] = s_rx[s_rx_rd];
        s_rx_rd = (s_rx_rd + 1u) % RX_SIZE;
    }
    return n;
}

static size_t g_room(void *ctx)
{
    (void)ctx;
    return TX_SIZE - (s_tx_head - s_tx_tail) - 1u;
}

static void g_write(void *ctx, const char *s, size_t n)
{
    (void)ctx;
    if (n > g_room(NULL)) { s_errors++; return; }
    for (size_t i = 0; i < n; i++) s_tx[(s_tx_head + i) & (TX_SIZE - 1u)] = (uint8_t)s[i];
    s_tx_head += (uint32_t)n;
}

static void g_poll(void *ctx)
{
    (void)ctx;
    if (s_tx_len != 0u || s_tx_head == s_tx_tail) return;
    const uint32_t at = s_tx_tail & (TX_SIZE - 1u);
    uint32_t len = s_tx_head - s_tx_tail;
    if (len > TX_SIZE - at) len = TX_SIZE - at;                 /* contiguous part only */
    if (len > 0xFFFFu) len = 0xFFFFu;
    s_tx_len = len;
    if (HAL_UART_Transmit_DMA(&BOARD_BRIDGE_UART, &s_tx[at], (uint16_t)len) != HAL_OK) {
        s_tx_len = 0u;                                           /* busy: retry next poll */
    }
}

static uint32_t g_errors(void *ctx) { (void)ctx; return s_errors; }

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *h)
{
    if (h != &BOARD_BRIDGE_UART) return;
    s_tx_tail += s_tx_len;
    s_tx_len = 0u;
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *h)
{
    if (h != &BOARD_BRIDGE_UART) return;
    s_errors++;
    /* An overrun or framing error stops DMA RX in the HAL: restart it. A partial
     * SLCAN line is answered with an error and the host's next command is clean. */
    if (h->RxState == HAL_UART_STATE_READY) start_rx();
    if (h->gState == HAL_UART_STATE_READY) s_tx_len = 0u;
}

const bridge_stream_t *stream_init(void)
{
    UART_HandleTypeDef *h = &BOARD_BRIDGE_UART;
    if (h->hdmarx == NULL || h->hdmatx == NULL) return NULL;         /* no DMA set up in CubeMX */
    if (h->hdmarx->Init.Mode != DMA_CIRCULAR) return NULL;           /* RX must be circular */

    h->Init.BaudRate = BOARD_BRIDGE_BAUD;
    if (HAL_UART_Init(h) != HAL_OK) return NULL;
    start_rx();

    s_stream = (bridge_stream_t){ .ctx = NULL, .read = g_read, .room = g_room, .write = g_write,
                                  .poll = g_poll, .errors = g_errors };
    return &s_stream;
}

#endif /* !BOARD_BRIDGE_USB */
