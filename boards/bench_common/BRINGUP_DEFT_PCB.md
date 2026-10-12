# Bench bring-up on two Deft Controls PCBs (STM32G474RET6)

Boss = `boards/bench_controller` (node 1). Node = `boards/bench_node` (node 10).
Both `board_config.h` already include `hw_deft_controls_pcb.h` (CH1 / FDCAN1, 85 MHz, LED PC7, input PB7).

## 0. Option bytes — do this on BOTH boards first
FDCAN1 RX is on **PB8 = BOOT0**. An idle CAN transceiver holds RX high, so with factory
option bytes the MCU boots the ROM bootloader instead of your firmware whenever CAN is
plugged in (symptom: works under the debugger, dead after a power cycle with CAN connected).

STM32CubeProgrammer → connect ST-Link → **OB** tab → User Configuration:
- `nSWBOOT0` = **unchecked** (0) → BOOT0 comes from the option bit, PB8 is ignored
- `nBOOT0` = **checked** (1) → boot from main flash
- Apply.

(The Deft Soft-DFU flow uses these same bits, so boards that ran Deft firmware are probably
already set — check anyway.)

## 1. CubeMX project (one project, two build configs or two copies)
Start from `DeftRoboticsControlsPCB.ioc` (File → Save Project As) so the clock tree and pins
are already right (HSE 25 MHz → 170 MHz, PCLK1 85 MHz, FDCAN1 on PB8/PB9). Then:
- **Middleware:** disable FREERTOS and USB_DEVICE. System Core → SYS → Timebase = **SysTick**.
- **Disable** SPI1, SPI3, UART4, UART5, TIM6, USB, FDCAN2, FDCAN3 (not needed for the bench).
- **FDCAN1:** keep. **NVIC → tick "FDCAN1 interrupt 0".** The Deft project polls and leaves
  this off; without it nothing is received. (The firmware stops in `bsp_fatal()` at boot
  with a message if you forget.)
- **PB7:** change it from GPIO_Output to **GPIO_Input, Pull-up**. The .ioc still says Output;
  the Deft repo fixed it by hand-editing `gpio.c`, and regenerating throws that edit away.
  `bsp_init()` reconfigures it anyway, but until then the MCU would drive the ESTOP net low.
- Keep FDCAN clock source = PCLK1 (what the Deft MSP code already does).
- Generate.

Add to the build: `core/*.c`, `ports/stm32g4/auvcan_port_g4.c`, `third_party/libcanard/canard.c`,
`generated/src/{uavcan.protocol.NodeStatus, uavcan.protocol.GetNodeInfo_req, uavcan.protocol.GetNodeInfo_res,
uavcan.protocol.RestartNode_req, uavcan.protocol.RestartNode_res, cuauv.Kill, cuauv.HostHeartbeat}.c`,
`boards/bench_common/bsp_g4.c`, and **one** of `boards/bench_controller/app.c` / `boards/bench_node/app.c`.
Include paths: `core`, `ports/stm32g4`, `third_party/libcanard`, `generated/include`,
`boards/bench_common`, and the matching `boards/bench_controller` or `boards/bench_node`.
Call `app_setup()` in USER CODE 2 and `app_loop()` inside `while (1)`.

## 2. Wiring
- CH1 connector of board A ↔ CH1 connector of board B: CANH–CANH, CANL–CANL, **GND–GND**.
- Exactly **two** 120 Ω terminators on the bus (check whether each PCB terminates CH1).
  Measure CANH–CANL with both boards off: **~60 Ω** is right; ~120 Ω = one missing; ~40 Ω = one too many.
- Boss kill/arm input = **PB7** (PDU hard-ESTOP net, pulled up). Touch PB7 to GND briefly = one press.
  Don't connect a PDU to that net during the bench test.

## 3. Expected behaviour (LED = PC7, the CH1 activity LED)
| Step | Boss LED | Node LED |
|---|---|---|
| Power both | slow blink (publishing killed) | off |
| Tap PB7→GND on boss | solid | solid (~30 ms) |
| Tap again | slow blink | off (~30 ms) |
| Arm, then unplug CANH/CANL | solid | off after ~250 ms |
| Reset boss | slow blink (boots killed) | stays off |
| **Fast blink (10 Hz) on either** | — | that board is error-passive: no ACKs → wiring, terminators, option bytes on the *other* board, clock |

## 4. If it doesn't come up
- **Board doesn't run at all with CAN plugged in, runs with it unplugged** → option bytes (step 0).
- **Stops in `bsp_fatal`** → read `why` in the debugger: IRQ not enabled, or wrong `BOARD_FDCAN_CLOCK_HZ`.
- **Both LEDs fast-blink** → neither sees the other: CANH/CANL swapped, no GND, no terminators,
  transceiver standby (the Deft firmware never drives one, so it should be tied on the board —
  confirm on the schematic; if an MCU pin drives it, set `BOARD_CAN_STBY_PORT/PIN` in the profile).
- **Debugger watch list:** `can.killed`, `g4.bus_off_count`, `g4.hw_lost`, `g4.rx_hp.dropped`,
  `g4.rx.dropped`; call `auvcan_get_bus_stats()` → rising `tec` = your frames aren't ACKed.
