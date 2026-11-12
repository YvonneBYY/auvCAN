/*
 * statuslog.h - a one-line CAN status report, once a second, as text.
 *
 *   t=12.0s n1 KILLED | bus ACTIVE TEC=0 REC=0 busoff=0 ovf=0 txerr=0 | rx 21/s tx 21/s | nodes 10:ok,K
 *
 *   KILLED/ARMED   this board's NodeStatus status-code bit 0 (bench convention)
 *   bus            ACTIVE / PASSIVE (TEC or REC >= 128) / BUSOFF, live counters,
 *                  bus-off events, RX ring overflows, TX frames dropped
 *   rx / tx        frames per second through this board's port (rx = after the filter)
 *   nodes          every node whose NodeStatus arrived in the last 3 s: id, health
 *                  (ok/WARN/ERR/CRIT), ",K" if its status-code bit 0 says killed.
 *                  "10:LOST" = seen before, silent for 3 s.
 *   hint           a plain-language guess when the counters match a known wiring fault
 *
 * The text goes to bsp_log(): USB virtual COM port on boards whose CubeMX project
 * has USB_DEVICE/CDC (log_usbcdc_g4.c), stdout on Linux with AUVCAN_STATUS=1,
 * nowhere otherwise. It never blocks: a line that doesn't fit is dropped.
 */
#ifndef STATUSLOG_H
#define STATUSLOG_H

#include "auvcan.h"

/* Before auvcan_start(): subscribes to NodeStatus for the node table. If the app
 * already handles NodeStatus itself, the table stays empty and the rest still works. */
void statuslog_init(auvcan_t *can);

/* Every app_loop(), after auvcan_poll(). Prints at 1 Hz and keeps the output moving. */
void statuslog_poll(auvcan_t *can);

/* Optional: the app appends its own fields after the bus/node part, e.g. the thruster's
 * PWM. fn writes at most size-1 chars into buf (NUL-terminated) and returns the length. */
typedef size_t (*statuslog_extra_fn)(char *buf, size_t size, void *user);
void statuslog_set_extra(statuslog_extra_fn fn, void *user);

/* Formats the line into buf (exposed for tests). Returns its length. */
size_t statuslog_format(auvcan_t *can, uint64_t now_us, char *buf, size_t size);

#endif
