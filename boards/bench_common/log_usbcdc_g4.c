/*
 * log_usbcdc_g4.c - bsp_log() over the board's USB connector, as a virtual COM port.
 *
 * Compiles to nothing unless the CubeMX project generated the USB CDC device
 * (usbd_cdc_if.h on the include path), so it is safe to export to every project.
 *
 * CubeMX (Deft PCB, STM32G474):
 *   Connectivity > USB: Device (FS)                      (PA11 = DM, PA12 = DP)
 *   Middleware > USB_DEVICE: Class = Communication Device Class (Virtual Port Com)
 *   Clock Configuration: "USB" mux = HSI48 (48 MHz)
 *   RCC > CRS SYNC: "CRS SYNC Source USB"                (trims HSI48 to the host's frames)
 *
 * On the Mac: ls /dev/cu.usbmodem*, then
 *   python -m serial.tools.miniterm /dev/cu.usbmodemXXXX     (baud rate is ignored)
 *
 * Never blocks. Text waits in a 2 KB ring; whole lines that don't fit are dropped
 * (no host reading, or the port closed). Each USB transfer is <= 63 bytes so no
 * zero-length packet is ever needed.
 */
#include "board_config.h"

#if defined(__has_include) && !defined(BOARD_LOG_UART) && !BOARD_BRIDGE_USB  /* UART log wins; the bridge owns USB */
#if __has_include("usbd_cdc_if.h")
#define LOG_USBCDC_AVAILABLE 1
#endif
#endif

#ifdef LOG_USBCDC_AVAILABLE

#include "bsp.h"

#include <string.h>

#include "main.h"
#include "usbd_cdc_if.h"

#define RING_SIZE 2048u                 /* power of two */
#define CHUNK_MAX 63u

extern USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t  s_ring[RING_SIZE];
static uint32_t s_head, s_tail;          /* main loop only */
static uint8_t  s_inflight[CHUNK_MAX];  /* owned by the USB stack while TxState != 0 */

static bool host_ready(void)
{
    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) return false;
    const USBD_CDC_HandleTypeDef *cdc = (const USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    return cdc != NULL && cdc->TxState == 0u;
}

void bsp_log(const char *s, size_t n)
{
    if (n > RING_SIZE - 1u - (s_head - s_tail)) return;          /* drop the whole line */
    for (size_t i = 0; i < n; i++) s_ring[(s_head + i) & (RING_SIZE - 1u)] = (uint8_t)s[i];
    s_head += (uint32_t)n;
}

void bsp_log_poll(void)
{
    if (s_head == s_tail || !host_ready()) return;
    uint32_t len = s_head - s_tail;
    if (len > CHUNK_MAX) len = CHUNK_MAX;
    for (uint32_t i = 0; i < len; i++) s_inflight[i] = s_ring[(s_tail + i) & (RING_SIZE - 1u)];
    if (CDC_Transmit_FS(s_inflight, (uint16_t)len) == USBD_OK) {
        s_tail += len;
    }
}

#endif
