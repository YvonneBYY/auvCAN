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
#include "auvcan_port.h"

/* Set up LED, button and the CAN port. Returns the port for auvcan_config_t,
 * or NULL on failure. Call first in app_setup(). */
const auvcan_port_t *bsp_init(void);

void bsp_led(bool on);

/* Raw button level, true while pressed. The app debounces it. */
bool bsp_button(void);

/* Unrecoverable setup error. Never returns. */
void bsp_fatal(const char *why);

#endif
