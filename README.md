# auvCAN core — v0.3.3 (core + bench node/controller apps + Deft PCB profile + thruster board + virtual sub)

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
  linux_mcast/              pydronecan-compatible UDP multicast bus ("mcast:0"), Linux + macOS
boards/template/            copy this to start a real board (app.c, board_config.h, CubeMX reference)
boards/thruster/            Thruster A (node 10) / B (node 11): RawCommand -> ramp -> PWM, ThrusterStatus
                            thruster.c = board logic (no HAL), pwm_g4.c / pwm_linux.c, README.md
boards/bench_node/          bench actuator: LED = armed/killed           (see "Bench demo" below)
boards/bench_controller/    bench node 1: button = kill, publishes cuauv.Kill
boards/bench_common/        app.h + tiny BSP (LED, button, CAN port): bsp_g4.c for G4 boards, bsp_linux.c for PC
                            hw_deft_controls_pcb.h + BRINGUP_DEFT_PCB.md: profile + checklist for the Deft G474 PCB
dsdl/cuauv/                 the 13 cuauv types from the message catalogue — IDs 20600-20699 reserved
generated/                  C code from dronecan_dsdlc (uavcan.* + cuauv.*); commit it with dsdl/
tests/test_core.c           215 checks on the simulated bus
tests/test_thruster.c       171 checks: ramp, kill, re-arm, stale, polarity, cmd[] slices, rejects
tests/dsdl_sizes.c          compile-time lock on every cuauv type's frame count
tools/virtual_node.c        run the real core as a board on your laptop
tools/bench.py              monitor / kill publisher / host heartbeat / thrust / restart / bring-up selftest
tools/sub.py                "enter the sub": launches every board on mcast:0, acts as the Jetson,
                            live board table + bus kill state; h/s/t/p/r keys (see --help)
tools/virtual_sub.sh        the same set of boards without the UI, logs followed in the terminal
tools/export_cubeide.sh     copy one firmware's files into a CubeIDE project (controller | node | thruster)
tools/fetch_deps.sh         generator deps (DSDL + dronecan_dsdlc) at pinned commits
third_party/libcanard/      libcanard @601ed35, vendored (3 files + LICENSE) — builds offline
```

## Quick start (no hardware)

```bash
python3 -m venv auvCAN && auvCAN/bin/pip install dronecan   # only for bench.py / sub.py
# tools/fetch_deps.sh                               # only needed to REGENERATE DSDL code
cmake -S . -B build && cmake --build build          # add -DAUVCAN_SANITIZE=OFF if ASan won't start (macOS + conda)
ctest --test-dir build                              # test_core + test_thruster (ASan/UBSan on)

./build/thruster_a_linux &                          # the real thruster firmware, on the virtual bus
python3 tools/bench.py selftest mcast:0 10 --actuator --thruster
python3 tools/bench.py monitor mcast:0              # or DroneCAN GUI Tool on "mcast:0"
```
On macOS, read "macOS notes" below first: the virtual bus needs a loopback route.

## Virtual sub

`tools/sub.py` launches every board as its own process on `mcast:0`, acts as the Jetson (node 2), and shows the whole bus live. Quitting stops every board it started.

```bash
python3 tools/sub.py                      # launch the sub and enter it
python3 tools/sub.py --attach             # watch a bus that's already running
python3 tools/sub.py --iface can0 --attach --no-host   # watch the real sub, change nothing
python3 tools/sub.py --snapshot 3         # no UI: run 3 s, print one screen
```

| Node | Program |
|---|---|
| 1 Controller | `bench_controller_host_linux`: arms only with a fresh host heartbeat |
| 2 Host | `sub.py` itself: HostHeartbeat 10 Hz, RawCommand 100 Hz |
| 10, 11 Thrusters | `thruster_a_linux`, `thruster_b_linux` |
| 20, 21, 30, 31 | `virtual_node` stand-ins (NodeStatus + GetNodeInfo only) until those boards have apps |

| Key | Does |
|---|---|
| `h` | Toggle hard kill (SIGUSR1 to the controller = its kill switch) |
| `s` | Toggle soft kill (in our HostHeartbeat) |
| `t` | Set thrust T1–T6, Orion units −255..255; T1–T3 go to Thruster A, T4–T6 to Thruster B |
| `0` | Zero thrust |
| ↑/↓ or `j`/`k` | Select a board |
| `p` | Power the selected board off / on |
| `r` | RestartNode the selected board; it reboots and rejoins killed |
| `q` | Quit |

The table's STATE comes from the fastest source: ThrusterStatus and Kill at 20 Hz, else NodeStatus bit 0 at 1 Hz. A board is OFFLINE after 3 s without NodeStatus. Board logs go to `/tmp/auvcan_sub/`.

`tools/virtual_sub.sh` starts the same boards without the UI and follows their logs; drive it with `bench.py thrust` and `kill -USR1 $(pgrep bench_controller)`.

## Thruster board

`boards/thruster/` is the first real board app. Build Thruster B with `-DTHRUSTER_B`. It ports Orion's `thrusters.c` with three fixes: a kill zeroes the ramp (re-arm ramps up from 0), `applied` reads 0 while killed, and ramp inc/dec are spin-up/spin-down. Polarity, ramp rates and the `cmd[]` offset are `#define`s in `board_config.h` until stage 3. Details, the behaviour table and G4 bring-up: `boards/thruster/README.md`. Scope the PWM pins before connecting an ESC.

## Bench demo

**On the Deft Controls PCB (G474RET6): follow `boards/bench_common/BRINGUP_DEFT_PCB.md`** — the bench
`board_config.h` files use that profile by default. Two must-dos it covers: option bytes (FDCAN1 RX is
the BOOT0 pin) and enabling FDCAN1 interrupt 0 (the Deft CubeMX project polls and leaves it off).
**On NUCLEO-G431KB (Nucleo-32): follow `boards/bench_common/BRINGUP_NUCLEO_G431KB.md`** and define
`BOARD_HW_NUCLEO_G431KB`. For a Nucleo-64 instead, build with `-DBOARD_NO_HW_PROFILE` and read on.

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
   For an existing app, `tools/export_cubeide.sh controller|node|thruster <project>` copies exactly the files it needs (core, port, libcanard, its DSDL sources, BSP, app) into `<project>/auvcan/` and prints the include paths.
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
| 4b Thruster | thruster firmware, PWM pins on a scope (no ESC yet) | `bench.py selftest can0 10 --actuator --thruster` all PASS; 1500 µs at boot and within one period of a kill |
| 5 Controller | controller firmware + gs_usb bridge (`BOARD_REQUIRE_HOST=1`) | Jetson sees `can0`; stopping HostHeartbeat kills the bus *(gs_usb bridge not in this drop)* |

## Design rules worth knowing

- **Actuators boot killed** and arm only on a fresh `cuauv.Kill(armed)` **from node 1**. Silence > 250 ms, an explicit kill, or a stuck sequence number all kill.
- One timestamp per `auvcan_poll()`. Handlers that feed an `auvcan_fresh_t` should use `can->poll_now_us`. `auvcan_fresh_ok()` also treats a slightly-older "now" as just fed (see the regression test for why).
- Keep `app_loop()` iterations under ~1 ms. Each RX ring holds 32 frames, and the G4 hardware FIFOs hold only 3.
- More than 7 broadcast subscriptions → the G4 port falls back to accept-all, and libcanard filters in software. That's correct, just more ISR load.
- Never run two node-1s: `bench.py kill` / `selftest --actuator|--thruster` act as node 1, so don't use them with a controller board on the bus (or with `sub.py`, which launches one).
- Never run two node-2s either: `bench.py thrust` / `host`, `selftest --thruster` and `sub.py` all act as the Jetson. Node IDs are not checked on the bus; two nodes with one ID just corrupt each other's traffic.
- Thrusters accept `RawCommand` only from node 2, and only if the array covers their slice (`cmd[0..2]` A, `cmd[3..5]` B). No command for 200 ms = ramp down to 0 and `stale`, but still armed.
- `cuauv.*` IDs live in **20600–20699**. The rest of 20000–21000 is partly taken by ArduPilot/com/cuav/mppt types that ship with pydronecan and the GUI Tool, which refuse to load duplicates.

## Changing DSDL

1. Edit or add `dsdl/cuauv/<ID>.<Name>.uavcan` (the filename prefix is the type ID; stay in 20600–20699).
2. Regenerate. The generator needs empy 3.3.4, which won't build against a system Python, so use a venv:
   ```bash
   tools/fetch_deps.sh
   python3 -m venv .gen && .gen/bin/pip install empy==3.3.4 pexpect
   .gen/bin/python third_party/dronecan_dsdlc/dronecan_dsdlc.py -O generated \
       third_party/dronecan_dsdl/uavcan dsdl/cuauv
   ```
3. Commit `dsdl/` and `generated/` together; add any new `generated/src/*.c` to `DSDL_SOURCES` in `CMakeLists.txt` and to the board projects that use it.
4. Rebuild: `tests/dsdl_sizes.c` fails the build if a change alters a type's frame count (update it deliberately when you mean to).
5. Reflash every node that uses the type. Python tools load `dsdl/cuauv/` at start-up and need nothing else.

A changed field list changes the type's signature. Multi-frame types then fail their CRC on any node built from the old definition; single-frame types (Kill, HostHeartbeat, ThrusterStatus) are caught only by the decoder's length check, so an old node silently drops a new Kill and never arms. Check that both sides agree:
```bash
grep "KILL_SIGNATURE " generated/include/cuauv.Kill.h
python3 -c "import dronecan; dronecan.load_dsdl('dsdl/cuauv'); print(hex(dronecan.TYPENAMES['cuauv.Kill'].get_data_type_signature()))"
```

## macOS notes

- **ASan aborts at start-up** (`CHECK failed: sanitizer_malloc_mac.inc`) under conda: build with `-DAUVCAN_SANITIZE=OFF`, or `conda deactivate` and rebuild with `CC=/usr/bin/clang` after `rm -rf build`.
- **Nothing shows on `mcast:0`**: give the terminal app Local Network access (System Settings → Privacy & Security → Local Network), then add a loopback route for the multicast group. The route is lost on reboot:
  ```bash
  sudo route -n add -net 239.0.0.0/8 -interface lo0
  ```
- The mcast port sends with multicast TTL 0, so the C nodes' traffic never leaves your machine. pydronecan (bench.py, sub.py) still sends with TTL 1; with the loopback route above, that stays local too. Without it, two people on one network running `mcast:0` share a bus.
- Don't type the `< >` around placeholders like `<controller pid>`: zsh reads them as redirection. `kill -USR1 $(pgrep bench_controller)` finds the pid for you.