# SLCAN bridge

A G4 board that puts your computer on the CAN bus: the `auv_controller` gateway. The host talks SLCAN text to it; the bridge forwards frames both ways. Every tool works through it unchanged: `bench.py`, `sub.py`, the DroneCAN GUI Tool. On the sub the laptop behind it is node 3 (debug).

Two ways to connect the computer, chosen at build time:

```
USB  (BOARD_BRIDGE_USB=1):  Mac ──USB-C cable──────────────────────> bridge board ──CANH/CANL/GND── bus
UART (default):             Mac ──USB── USB-UART cable ──TX/RX/GND──> bridge board ──CANH/CANL/GND── bus
```

Use USB on any board with a USB connector (the CAN test board, the Deft PCB): no adapter, and it shows up like a commercial USB-CAN adapter.

The bridge is not a DroneCAN node: it has no node ID and sends nothing of its own. It runs at 1 Mbit/s only, and passes extended frames only (DroneCAN uses nothing else).

| File | What |
|---|---|
| `slcan.c/.h` | SLCAN protocol: commands in, frames out. No HAL; tested by `tests/test_slcan.c` |
| `app.c` | The bridge: SLCAN ↔ the G4 FDCAN port, a 64-frame queue toward the bus |
| `stream_usbcdc_g4.c` | The USB link (`BOARD_BRIDGE_USB=1`): USB CDC virtual serial port, 1 KB RX ring with flow control, 4 KB TX ring. Tested by `tests/test_usbcdc.c` against a fake ST USB stack |
| `stream_g4.c` | The UART link (default): circular DMA RX, DMA TX from a 4 KB ring |
| `stream_linux.c` | A pseudo-terminal instead of a UART, for testing on a PC |
| `board_config.h` | USB or UART link, which UART and its baud rate, the hardware profile (Deft PCB by default) |

## Using it

**USB link:** the board appears as `/dev/cu.usbmodem…` (macOS) or `/dev/ttyACM…` (Linux). No baud rate needed; any value is accepted and ignored:

```bash
ls /dev/cu.usbmodem*                     # plug the board in and look for the new entry
python tools/bench.py monitor slcan:/dev/cu.usbmodemXXXX
python tools/sub.py --iface slcan:/dev/cu.usbmodemXXXX --attach      # read-only dashboard as node 3
python tools/bench.py opkill slcan:/dev/cu.usbmodemXXXX --kill       # latched operator kill
```

An ST-Link also shows up as a `usbmodem` port: compare the list before and after plugging the bridge in.

**UART link:** address the bridge as `slcan:<device>@<baud>`:

```bash
python tools/bench.py monitor slcan:/dev/cu.usbserial-XXXX@921600
python tools/bench.py selftest slcan:/dev/cu.usbserial-XXXX@921600 10 --actuator --thruster
python tools/stress_nodeinfo.py slcan:/dev/cu.usbserial-XXXX@921600 10
python tools/sub.py --iface slcan:/dev/cu.usbserial-XXXX@921600 --attach
```

- `@921600` must match `BOARD_BRIDGE_BAUD`. Without it, pydronecan opens the port at 3,000,000 baud, the right rate for USB adapters but not for this UART.
- The `slcan:` prefix tells pydronecan the driver outright. Without it, a `/dev/…` path is probed for MAVLink first, and any other path is taken for SocketCAN.
- One program at a time: a serial port can't be shared. `selftest --thruster` is node 2 and sends node 1's Kill frames through the same connection, so it needs nothing else on the bus. `sub.py --attach` with a real controller board on the bus gives the interactive view.
- `ls /dev/cu.usbserial*` lists FTDI cables on macOS; the name stays the same per cable.

The status LED is on while a host has the channel open. The heartbeat LED blinks at 1 Hz; a fast blink means `bsp_fatal()`, so read `bsp_fatal_why` in the debugger.

## Building it with the USB link

1. **CubeMX** (same `.ioc` base as the bench boards: FDCAN with "FDCANx interrupt 0", SysTick timebase):
   - Connectivity → **USB**: Device (FS). On the G474 it is fixed to **PA11 (DM) / PA12 (DP)**, so CAN must be on other pins (Deft: CH1 PB8/PB9 or CH2 PA8/PA15).
   - Middleware → **USB_DEVICE**: Class = **Communication Device Class (Virtual Port Com)**.
   - Clock Configuration: USB clock mux = **HSI48**. RCC → **CRS SYNC = USB_FS**.
   - No UART or DMA needed.
2. **Define symbols** (CubeIDE → Preprocessor): `BOARD_BRIDGE_USB=1`, plus `BOARD_DEFT_CAN_CH=2` if CAN is on CH2.
3. **Export**: `tools/export_cubeide.sh slcan_bridge <project>`, include paths as printed.
4. **`main.c`**: `app_setup();` in USER CODE BEGIN 2 (after `MX_USB_Device_Init()`), `app_loop();` in `while (1)`.

Nothing in the generated USB files needs editing: `stream_init()` registers the bridge's own CDC callbacks in place of the ones in `usbd_cdc_if.c`.

Flow control: when the host sends faster than the bridge forwards (a full 1 KB RX ring), the USB OUT endpoint isn't re-armed, so the host is held off (NAK) instead of losing bytes. With no host connected, replies are discarded instead of piling up.

## USB-C connector (TYPE-C 16PIN 2MD(073) or any 16-pin USB 2.0 receptacle)

| Pins | Connect to | Why |
|---|---|---|
| **CC1 (A5)**, **CC2 (B5)** | **5.1 kΩ to GND, one resistor each** | Marks the board as a device (sink), so a USB-C host turns VBUS on. Without them a **C-to-C cable gives no power and no connection** (A-to-C still works, which hides the mistake). Don't tie CC1 and CC2 together. |
| **D+ (A6, B6)** | both to **PA12** | Plug works either way round; keep the stub joining the two rows short |
| **D− (A7, B7)** | both to **PA11** | Same |
| VBUS (A4, A9, B4, B9) | 5 V net, see below | |
| GND (A1, A12, B1, B12) | GND | |
| SBU1/SBU2 (A8, B8) | not connected | |
| Shell / mounting tabs | GND (or through 1 MΩ ∥ 4.7 nF) | Solder all tabs: a right-angle SMD connector carries the cable's leverage |

- **No external pull-up on D+**: the G474's USB peripheral switches in its own 1.5 kΩ. An extra one breaks enumeration.
- **No series resistors** on D+/D−. Route them as a short pair, away from switching regulators and the CAN lines; at full speed (12 Mbit/s) length matching isn't critical.
- **ESD**: a USB ESD array (USBLC6-2SC6, TPD2E2U06) on D+/D−/VBUS right at the connector. A port people plug laptops into will see static.
- **VBUS**: if the board is also powered from the backplane, keep VBUS from back-feeding the sub's 5 V (diode / ideal-diode / power mux), or don't power the board from VBUS at all. The G474 has no VBUS sensing, so nothing else needs VBUS.
- **Rated** 5 V / 3 A, 10 000 mating cycles, −25…+85 °C: fine for a debug port.

## Putting it on a Deft PCB (UART link)

1. **CubeMX**: start from the same `.ioc` as the bench boards (`boards/bench_common/BRINGUP_DEFT_PCB.md` step 1: FDCAN1 with "FDCAN1 interrupt 0", SysTick timebase, PB7 input).
2. **A USART for the host link**, on two pins you can reach: Mode **Asynchronous**, 8 data bits, no parity, 1 stop bit.
   - DMA Settings: add **USARTx_RX** with Mode **Circular**, and **USARTx_TX** with Mode **Normal**.
   - NVIC: enable the **USARTx global interrupt** (the DMA channel interrupts are enabled with the DMA requests).
   - Leave the baud rate; `stream_g4.c` sets it from `BOARD_BRIDGE_BAUD`.
3. **`board_config.h`**: `BOARD_BRIDGE_UART` = that USART's handle (`huart1`, `huart2`, …). `BOARD_FDCAN_CLOCK_HZ` comes from the Deft profile.
4. **Export**: `tools/export_cubeide.sh slcan_bridge <project>`, then add the printed include paths.
5. **`main.c`**: `#include "app.h"`; `app_setup();` in USER CODE BEGIN 2 (after all `MX_…_Init()`); `app_loop();` inside `while (1)`.
6. **Option bytes**: same as the bench boards (`nSWBOOT0` unchecked, `nBOOT0` checked).

`stream_g4.c` refuses to start if the UART has no DMA or RX isn't circular: the heartbeat LED fast-blinks and `bsp_fatal_why` says so. It owns `HAL_UART_TxCpltCallback` and `HAL_UART_ErrorCallback`; don't define them elsewhere.

## Wiring

- **USB-UART cable**, 3.3 V logic (an FTDI TTL-232R-3V3 or similar): cable **TX → board RX**, cable **RX → board TX**, **GND → GND**. Don't connect the cable's power pin.
- **CAN**: bridge CH1 ↔ the other board's CH1: CANH–CANH, CANL–CANL, GND–GND, and exactly two 120 Ω terminators on the bus (about 60 Ω between CANH and CANL with everything off).
- The thruster board stays on its ST-Link for flashing and the debugger.

## Bring-up

1. Bridge alone, cable connected: `python tools/bench.py monitor slcan:/dev/cu.usbserial-XXXX@921600` starts with no error and the bridge's status LED turns on. An "SLCAN ACK timeout" means no answer from the bridge: swap TX/RX, check the baud rate and the device name.
2. Add the thruster board on CAN: `monitor` lists node 10.
3. `selftest … 10 --actuator --thruster`: 14/14, with the scope on the PWM pins.
4. `stress_nodeinfo.py … 10`: every request answered. This is the check on the FDCAN FIFO-mode change, on both boards.

## Limits

- **Throughput**: USB full speed carries far more than the bus can produce. Over the UART, 921600 baud carries about 3,400 frames/s toward the host; the catalogue's bus budget is about 436 frames/s. A burst the UART can't keep up with is dropped and reported by the SLCAN `F` command (`F01`).
- **Toward the bus**: 64 frames of queue, then the 3 FDCAN TX buffers. A full queue answers the host's `T` with an error, and `F` reports `F02`.
- **Not supported**: other bit rates (`S0`–`S7`), listen-only (`L`), standard and remote frames, timestamps (`Z1` is accepted and ignored).
