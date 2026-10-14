# auvCAN core — v0.3 (core + bench node/controller apps + Deft PCB profile)

auvCAN = **DroneCAN v0 on CAN 2.0B at 1 Mbps** + the `cuauv.*` message namespace + the safety conventions carried over from toucan/serial (kill watchdog, host timeout, one config per board).

This repo is the part **every board shares**. A board adds only its application code on top (see `boards/template/`).

## Layout

```
core/                       portable, no HAL — unit-tested on a PC
  auvcan.h/.c               node core: RX/TX pump, NodeStatus, GetNodeInfo, RestartNode,
                            Kill watchdog, subscription table -> hardware filters
  auvcan_spec.h             node IDs, priority classes, timeouts, rates (the spec, in code)
  auvcan_fresh.h/.c         "is this subscription alive?" — Kill, RawCommand staleness, host heartbeat
  can_ring.h/.c             ISR->main SPSC ring (ported + fixed from toucan/queue.c)
  auvcan_bittiming.h        1 Mbps @ 87.5 % timing from any kernel clock
  auvcan_port.h             the interface a hardware port implements
ports/
  stm32g4/                  FDCAN driver (classic CAN, queue-mode TX, 2 RX lanes, bus-off recovery)
  sim/                      in-process virtual bus for unit tests
  linux_mcast/              pydronecan-compatible UDP multicast bus ("mcast:0")
boards/template/            copy this to start a real board (app.c, board_config.h, CubeMX reference)
boards/bench_node/          bench actuator: LED = armed/killed           (see "Bench demo" below)
boards/bench_controller/    bench node 1: button = kill, publishes cuauv.Kill
boards/bench_common/        app.h + tiny BSP (LED, button, CAN port): bsp_g4.c for G4 boards, bsp_linux.c for PC
                            hw_deft_controls_pcb.h + BRINGUP_DEFT_PCB.md: profile + checklist for the Deft G474 PCB
dsdl/cuauv/                 cuauv.Kill (20600), cuauv.HostHeartbeat (20601) — IDs 20600-20699 reserved
generated/                  C code from dronecan_dsdlc (uavcan.* + cuauv.*)
tests/test_core.c           213 checks on the simulated bus
tools/virtual_node.c        run the real core as a board on your laptop
tools/bench.py              monitor / kill publisher / host heartbeat / restart / bring-up selftest
tools/fetch_deps.sh         generator deps (DSDL + dronecan_dsdlc) at pinned commits
third_party/libcanard/      libcanard @601ed35, vendored (3 files + LICENSE) — builds offline
```

## Quick start (no hardware)

```bash
pip install dronecan                                # only for bench.py
# tools/fetch_deps.sh                               # only needed to REGENERATE DSDL code
cmake -S . -B build && cmake --build build
./build/test_core                                   # unit tests (ASan/UBSan on)

./build/virtual_node 10 edu.cornell.cuauv.thruster_a --actuator &
python3 tools/bench.py selftest mcast:0 10 --actuator
python3 tools/bench.py monitor mcast:0              # or DroneCAN GUI Tool on "mcast:0"
```

### On two Nucleos

Two G4 Nucleo-64s, two 3.3 V transceivers (standby pin tied low), twisted pair, 120 Ω at each end, common ground. PA12 = FDCAN1_TX → TXD, PA11 = FDCAN1_RX ← RXD. A USB-CAN adapter (gs_usb) on the bus is strongly recommended.

| Board | Folder | Behaviour |
|---|---|---|
| Controller (node 1) | `boards/bench_controller/` | **Boots killed.** B1 toggles kill/arm. Publishes `cuauv.Kill` at 20 Hz and instantly on change. LED on = publishing armed, slow blink = publishing killed. |
| Node (node 10) | `boards/bench_node/` | LED on = armed, off = killed, **fast blink = bus trouble** (error-passive: no ACKs / wiring / bit rate). |

Per board, in CubeMX: FDCAN1 classic on PA11/PA12, FDCAN clock = HSE, NVIC FDCAN1 interrupt 0. Then add the auvCAN sources (next section) plus `boards/bench_common/bsp_g4.c` and that board's `app.c`, with include paths `boards/<app>/` and `boards/bench_common/`. LED (PA5) and button (PC13) are set up by `bsp_g4.c`, no CubeMX needed. **Set `BOARD_FDCAN_CLOCK_HZ` in each `board_config.h` to your HSE** (default 24 MHz). If the controller toggles on button *release*, set `BOARD_BTN_ACTIVE_HIGH 0`.

Expected results:
1. Power both: node LED off, controller LED slow-blinking. `bench.py monitor can0` lists nodes 1 and 10.
2. Press B1 → both LEDs solid on within ~30 ms.
3. Press B1 again → node LED off within ~30 ms.
4. Arm, then unplug the controller's CANH/CANL → node LED off after **~250 ms** (watchdog).
5. Reset the controller → it boots killed; the node stays off until you press B1.

The same two apps run on your laptop, no hardware:
```bash
./build/bench_node_linux &
./build/bench_controller_linux &          # prints its pid
kill -USR1 <controller pid>               # = press B1
python3 tools/bench.py monitor mcast:0
```
With `BOARD_REQUIRE_HOST=1` (`bench_controller_host_linux`) the controller only arms while `cuauv.HostHeartbeat` from node 2 is fresh: try `python3 tools/bench.py host mcast:0` (and `--soft-kill`).

## Putting it on a G4 board

1. **CubeMX**: enable FDCAN1 (classic). Clock mux = **HSE** or PLL from HSE (never HSI). NVIC: enable **FDCAN1 interrupt 0** (interrupt 1 is unused; every FDCAN interrupt stays on line 0 so one ISR feeds both RX rings). Compare against `boards/template/cubemx_reference/fdcan_msp_reference.c`.
2. Add to the project:
   - sources: `core/*.c`, `ports/stm32g4/auvcan_port_g4.c`, `third_party/libcanard/canard.c`, and from `generated/src/`: `uavcan.protocol.NodeStatus.c`, `uavcan.protocol.GetNodeInfo_{req,res}.c`, `uavcan.protocol.RestartNode_{req,res}.c`, `cuauv.Kill.c`, plus whatever your board publishes.
   - include paths: `core/`, `ports/stm32g4/`, `third_party/libcanard/`, `generated/include/`
3. Copy `boards/template/{app.c,app.h,board_config.h}`, set the node ID, name and `BOARD_FDCAN_CLOCK_HZ`, then call `app_setup()` / `app_loop()` from `main.c`.
4. The port **owns** `HAL_FDCAN_RxFifo0Callback`, `HAL_FDCAN_RxFifo1Callback` and `HAL_FDCAN_ErrorStatusCallback`. Don't define them anywhere else.

**Exclude from the CubeIDE build:** `ports/sim/`, `ports/linux_mcast/`, `tests/`, `tools/`, `boards/bench_common/bsp_linux.c`, `boards/template/cubemx_reference/` (and any `boards/*` you aren't building). CubeIDE compiles every `.c` it can see.

Measured footprint (STM32G431, `-Os`, including the HAL and a 2 KB stack): core ≈ **14.2 KB flash, 6.7 KB RAM**; bench controller with host heartbeat ≈ 15.3 KB / 6.8 KB.

## Bring-up checklist

| Stage | Setup | Pass when |
|---|---|---|
| 0 Raw CAN | 2 boards + transceivers + USB-CAN adapter (gs_usb) | `candump can0` shows frames at 1 Mbps |
| 1 Driver | same | 2 boards echo with no loss under load; short CANH-CANL → bus-off → rejoins after you remove the short (NodeStatus goes WARNING for 1 s) |
| 2 Node services | + `bench.py` or the GUI Tool | `bench.py selftest can0 <id>` stage 2 all PASS |
| 3 Params | *(next step, not in this drop)* | GUI Tool reads/writes params, values survive a reset |
| 4 Safety | `bench_controller` board, **or** `bench.py` as node 1 if there's no controller | Bench demo steps 1–5 above; or `bench.py selftest can0 <id> --actuator` |
| 5 Controller | controller firmware + gs_usb bridge (`BOARD_REQUIRE_HOST=1`) | Jetson sees `can0`; stopping HostHeartbeat kills the bus *(gs_usb bridge not in this drop)* |

## Design rules worth knowing

- **Actuators boot killed** and arm only on a fresh `cuauv.Kill(armed)` **from node 1**. Silence > 250 ms, an explicit kill, or a stuck sequence number all kill.
- One timestamp per `auvcan_poll()`. Handlers that feed an `auvcan_fresh_t` should use `can->poll_now_us`. `auvcan_fresh_ok()` also treats a slightly-older "now" as just fed (see the regression test for why).
- Keep `app_loop()` iterations under ~1 ms. Each RX ring holds 32 frames, and the G4 hardware FIFOs hold only 3.
- More than 7 broadcast subscriptions → the G4 port falls back to accept-all, and libcanard filters in software. That's correct, just more ISR load.
- Never run two node-1s: `bench.py kill` / `selftest --actuator` act as node 1, so don't use them with a controller board on the bus.
- `cuauv.*` IDs live in **20600–20699**. The rest of 20000–21000 is partly taken by ArduPilot/com/cuav/mppt types that ship with pydronecan and the GUI Tool, which refuse to load duplicates.
