# Bench bring-up on NUCLEO-G431KB (STM32G431KBT6, Nucleo-32)

Same bench apps as the Deft PCB: boss = `bench_controller` (node 1), node = `bench_node` (node 10).
Profile: `boards/bench_common/hw_nucleo_g431kb.h`, selected with the preprocessor symbol
`BOARD_HW_NUCLEO_G431KB`. You can also mix: Deft PCB as boss + Nucleo as node (same bus, same 1 Mbps).

## 0. Hardware you need per board
- A **3.3 V CAN transceiver** breakout (SN65HVD230, TCAN1042V/TCAN332, MCP2562 with VIO = 3.3 V).
  The Nucleo has none.
- Wiring (Nucleo → transceiver): **PA12 / D2 → TXD**, **PA11 / D10 ← RXD**, 3V3, GND.
  Standby / S / Rs pin → **GND** (normal mode).
- Bus: CANH–CANH, CANL–CANL, GND–GND between the two transceivers; 120 Ω at each end
  (check the module — many have one fitted). Both boards off: CANH–CANL ≈ 60 Ω.
- Kill/arm input on the boss: **PA0 / A0** to GND = one press (or set `hard_kill` in Live Expressions).

No option-byte step: FDCAN1 is on PA11/PA12 here, not on the BOOT0 pin.

## 1. CubeMX project (per board, or generate once and copy Core/ like on the Deft boards)
1. **File → New Project → Board Selector → NUCLEO-G431KB.** Answer **No** to "initialize all
   peripherals with their default mode" (keeps USART/LPUART etc. out).
2. **System Core → SYS:** Debug = Serial Wire, Timebase = SysTick.
3. **System Core → RCC:** keep the HSE setting the Board Selector chose.
4. **Connectivity → FDCAN1:** Activated, on **PA11 (RX) / PA12 (TX)**. NVIC Settings: tick
   **FDCAN1 interrupt 0**. Leave bit timing alone (the driver sets it).
5. **Clock Configuration:** PLL Source = **HSE**, SYSCLK = 170 MHz from PLLCLK, APB1 /2 →
   PCLK1 = 85 MHz, FDCAN clock = PCLK1. Don't click "Resolve Clock Issues".
   Check **"To FDCAN (MHz)" = 85**. Any other whole-MHz value: set `BOARD_FDCAN_CLOCK_HZ`.
6. **Project Manager:** Toolchain = STM32CubeIDE → Generate Code.

## 2. auvCAN files and settings (same as the Deft boards)
```
sh tools/export_cubeide.sh controller <path>/<boss project>
sh tools/export_cubeide.sh node       <path>/<node project>
```
Then in each project: Source Location += `auvcan`, the six include paths, `main.c` hooks
(`#include "app.h"`, `app_setup();`, `app_loop();`) — and one extra step:

**Project → Properties → C/C++ Build → Settings → MCU GCC Compiler → Preprocessor →
Define symbols → add `BOARD_HW_NUCLEO_G431KB`** (All configurations).

Check: `bsp/hw_nucleo_g431kb.h` exists in the exported folder; the build log shows
`-DBOARD_HW_NUCLEO_G431KB`.

## 3. Expected behaviour (LD2 green, PB8 — the only user LED)
| Step | Boss LD2 | Node LD2 |
|---|---|---|
| Power both | 2 Hz blink (killed) | off |
| A0 → GND on boss (or `hard_kill = 0`) | solid | solid |
| Again (`hard_kill = 1`) | 2 Hz blink | off |
| Armed, unplug CANH | 10 Hz blink (no ACKs) | off within ~250 ms |
| 10 Hz blink with both connected | — | wiring, transceiver standby pin, termination, clock |

No heartbeat LED on this board (one user LED). If nothing lights at all: pause in the
debugger and check the call stack (`Error_Handler` from `SystemClock_Config` = HSE didn't start;
`bsp_fatal` = read `bsp_fatal_why`).
