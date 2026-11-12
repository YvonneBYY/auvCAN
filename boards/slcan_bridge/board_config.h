/*
 * board_config.h - SLCAN bridge: a G4 board that puts a computer on the CAN bus.
 *
 * The host (bench.py, sub.py, the GUI Tool, slcand) talks SLCAN over the board's own
 * USB port (BOARD_BRIDGE_USB=1) or over a UART + USB-UART cable (default); this board
 * forwards frames both ways. It is not a DroneCAN node: it has no node ID
 * and sends nothing of its own.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "auvcan_spec.h"

/* LEDs + FDCAN pins + clock from the bench hardware profile (Deft PCB by default). */
#if defined(BOARD_HW_NUCLEO_G431KB)
#include "hw_nucleo_g431kb.h"
#elif !defined(BOARD_NO_HW_PROFILE)
#include "hw_deft_controls_pcb.h"
#endif

#define BOARD_NODE_ID         0          /* not a node; only seeds the Linux build's fake UID */
#define BOARD_APP_NAME        "slcan_bridge"

#ifndef BOARD_FDCAN_CLOCK_HZ
#define BOARD_FDCAN_CLOCK_HZ  24000000u
#endif

/* ---- Link to the host -------------------------------------------------------
 * 1 = USB CDC on the board's USB connector (stream_usbcdc_g4.c): no cable adapter,
 *     shows up as /dev/cu.usbmodem…, baud rate ignored. CubeMX: USB Device (FS) +
 *     USB_DEVICE CDC, USB clock = HSI48, CRS SYNC = USB_FS. Hardware: README.
 * 0 = UART (stream_g4.c), below. */
#ifndef BOARD_BRIDGE_USB
#define BOARD_BRIDGE_USB      0
#endif

/* ---- UART link (stream_g4.c, BOARD_BRIDGE_USB=0) ---------------------------
 * CubeMX: the USART named here in Asynchronous mode, with DMA on RX in CIRCULAR
 * mode and DMA on TX in NORMAL mode, and the USART global interrupt enabled.
 * stream_g4.c sets the baud rate itself. The host must use the same rate:
 *   python tools/bench.py monitor /dev/cu.usbserial-XXXX@921600            */
#ifndef BOARD_BRIDGE_UART
#define BOARD_BRIDGE_UART     huart1
#endif
#ifndef BOARD_BRIDGE_BAUD
#define BOARD_BRIDGE_BAUD     921600u
#endif

#endif
