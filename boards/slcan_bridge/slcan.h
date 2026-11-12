/*
 * slcan.h - SLCAN (Lawicel ASCII) protocol, no HAL, no I/O: unit-tested on a PC.
 *
 * The host sends commands terminated by '\r'; each gets '\r' (OK) or '\a' (error).
 * Supported, as pydronecan / slcand / CANable firmware use them:
 *
 *   ""            empty line: OK (pydronecan sends one to reset the parser)
 *   S0..S8        bit rate: only S8 (1 Mbit/s) is accepted - auvCAN runs at 1 Mbit/s only
 *   O / C         open / close the channel (frames are bridged only while open)
 *   L             listen-only: refused (the G4 port is always in normal mode)
 *   T iiiiiiii l dd..   send an extended frame -> "Z\r"   (only while open)
 *   t iii l dd..        standard frame: refused (DroneCAN is extended-only)
 *   F             status flags -> "Fxx\r", then cleared
 *   V / v / N     version / firmware / serial -> "V1013\r", "vAUVCAN\r", "NAUV1\r"
 *   Z0/Z1, M.., m..     accepted and ignored (no timestamps, no filtering)
 *
 * Frames from the bus go to the host as "T iiiiiiii l dd..\r" (no timestamp).
 */
#ifndef SLCAN_H
#define SLCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <canard.h>

#define SLCAN_MAX_LINE   32          /* "T" + 8 id + 1 dlc + 16 data + slack */
#define SLCAN_FRAME_TEXT 27          /* longest frame line incl. '\r' */

/* Status flags reported by 'F' (Lawicel bit positions). */
#define SLCAN_F_RX_FULL      0x01u   /* frames dropped toward the host */
#define SLCAN_F_TX_FULL      0x02u   /* a 'T' was refused: CAN TX full */
#define SLCAN_F_ERR_WARNING  0x04u
#define SLCAN_F_OVERRUN      0x08u   /* CAN RX overflow in the port */
#define SLCAN_F_ERR_PASSIVE  0x20u
#define SLCAN_F_BUS_ERROR    0x80u   /* bus-off seen */

typedef struct {
    void *ctx;
    /* Queue a frame on the bus: 1 = queued, 0 = TX full, <0 = rejected. */
    int  (*send_frame)(void *ctx, const CanardCANFrame *f);
    /* Write bytes to the host. */
    void (*write)(void *ctx, const char *s, size_t n);
    /* Current port flags (SLCAN_F_* from bus stats), may be NULL. */
    uint8_t (*port_flags)(void *ctx);
} slcan_io_t;

typedef struct {
    slcan_io_t io;
    char       line[SLCAN_MAX_LINE];
    uint8_t    len;
    bool       overlong;             /* current line exceeded the buffer: answer error at '\r' */
    bool       open;
    uint8_t    flags;                /* sticky SLCAN_F_* bits since the last 'F' */
    /* counters */
    uint32_t   tx_frames, rx_frames, errors;
} slcan_t;

void   slcan_init(slcan_t *s, const slcan_io_t *io);
void   slcan_feed(slcan_t *s, const uint8_t *data, size_t n);     /* bytes from the host */
size_t slcan_format(const CanardCANFrame *f, char out[SLCAN_FRAME_TEXT]);  /* bus frame -> text */
void   slcan_note(slcan_t *s, uint8_t flags);                     /* raise sticky flags */

#endif
