/*
 * stream.h - the bridge's serial link to the host, one implementation per target:
 *   stream_g4.c        STM32G4 UART: circular DMA RX, DMA TX from a ring
 *   stream_usbcdc_g4.c STM32G4 USB CDC on the board's USB port (BOARD_BRIDGE_USB=1)
 *   stream_linux.c  a pseudo-terminal (/dev/pts/N) for testing on a PC
 */
#ifndef STREAM_H
#define STREAM_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    void   *ctx;
    size_t (*read)(void *ctx, uint8_t *buf, size_t max);      /* non-blocking; 0 = nothing */
    size_t (*room)(void *ctx);                                /* bytes write() can take now */
    void   (*write)(void *ctx, const char *s, size_t n);      /* drops what doesn't fit */
    void   (*poll)(void *ctx);                                /* start pending TX */
    uint32_t (*errors)(void *ctx);                            /* link errors (UART overrun/framing, bytes dropped) */
} bridge_stream_t;

const bridge_stream_t *stream_init(void);                     /* NULL on failure */

#endif
