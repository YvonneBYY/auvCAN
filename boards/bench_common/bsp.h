/*
 * bsp.h - the little bit of board I/O the bench apps need.
 *
 * Two implementations, same app.c on both:
 *   bsp_g4.c     - STM32G4 Nucleo: LED + user button GPIO, FDCAN1 via the G4 port
 *   bsp_linux.c  - Linux process on the "mcast:0" virtual bus: LED changes are
 *                  printed, the button is pressed with `kill -USR1 <pid>`
 *
 * Each app's board_config.h supplies BOARD_NODE_ID etc.
 */
#ifndef BSP_H
#define BSP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "auvcan_port.h"

/* Set up LED, button and the CAN port. Returns the port for auvcan_config_t,
 * or NULL on failure. Call first in app_setup(). */
const auvcan_port_t *bsp_init(void);

void bsp_led(bool on);

/* Raw button level, true while pressed. The app debounces it. */
bool bsp_button(void);

/* Heartbeat LED: call every loop. Toggles every 500 ms (1 Hz blink) so you can
 * see the firmware is running. No-op on boards without one. */
void bsp_heartbeat(uint64_t now_us);

/* Unrecoverable setup error. Never returns. Blinks the heartbeat LED fast
 * (if the board has one) so a stuck board is distinguishable from a hung one. */
void bsp_fatal(const char *why);

/* Text output for statuslog.c. Never blocks; may drop. G4: USB virtual COM port
 * when the project has USB CDC (log_usbcdc_g4.c), else discarded. Linux: stdout
 * when AUVCAN_STATUS=1. bsp_log_poll() moves queued text out; call every loop. */
void bsp_log(const char *s, size_t n);
void bsp_log_poll(void);

#endif
