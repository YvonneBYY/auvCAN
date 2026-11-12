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

## Notes

- **Actuators boot killed** and arm only on a fresh `cuauv.Kill(armed)` **from node 1**. Silence > 250 ms, an explicit kill, or a stuck sequence number all kill.
- One timestamp per `auvcan_poll()`. Handlers that feed an `auvcan_fresh_t` should use `can->poll_now_us`. `auvcan_fresh_ok()` also treats a slightly-older "now" as just fed (see the regression test for why).
- Keep `app_loop()` iterations under ~1 ms. Each RX ring holds 32 frames, and the G4 hardware FIFOs hold only 3.
- More than 7 broadcast subscriptions → the G4 port falls back to accept-all, and libcanard filters in software. That's correct, just more ISR load.
- Never run two node-1s: `bench.py kill` / `selftest --actuator|--thruster` act as node 1, so don't use them with a controller board on the bus (or with `sub.py`, which launches one).
- Never run two node-2s either: `bench.py thrust` / `host`, `selftest --thruster` and `sub.py` all act as the Jetson. Node IDs are not checked on the bus; two nodes with one ID just corrupt each other's traffic.
- Thrusters accept `RawCommand` only from node 2, and only if the array covers their slice (`cmd[0..2]` A, `cmd[3..5]` B). No command for 200 ms = ramp down to 0 and `stale`, but still armed.
- `cuauv.*` IDs live in **20600–20699**. The rest of 20000–21000 is partly taken by ArduPilot/com/cuav/mppt types that ship with pydronecan and the GUI Tool, which refuse to load duplicates.
