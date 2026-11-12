# Thruster module

**There is no thruster board any more.** The 8 ESC outputs are on the **sensor board (node 30)**, which runs this module next to its sensors: see `boards/sensor/README.md` for the board, its pin-out and bring-up. (Node 10, and before it the A/B split on nodes 10 and 11, are retired.)

This folder is the thruster logic the sensor board builds in: T1–T8 from `RawCommand.cmd[0..7]`.

| File | What |
|---|---|
| `thruster.c/.h` | RawCommand → ramp → PWM, kill handling, ThrusterStatus. No HAL. Sets only NodeStatus status-code bit 0 (killed); `thruster_warning()` tells the app when to report health WARNING. |
| `pwm.h`, `pwm_g4.c` / `pwm_linux.c` | ESC outputs: G4 timers / printed pulse widths. The output table (`BOARD_PWM_OUTPUTS`) is in the sensor board's `board_config.h`. |
| `tests/test_thruster.c` | 174 checks on the logic |

## Behaviour

| Situation | Output | ThrusterStatus | NodeStatus |
|---|---|---|---|
| Boot | 1500 µs (neutral) | `killed` | code bit 0 = 1, health OK |
| Armed, fresh commands | ramps toward the command, `inc`/`dec` per 10 ms | `applied` = ramped value | health OK |
| Kill (explicit, or no Kill for 250 ms) | 1500 µs **immediately**, ramp state zeroed | `killed`, `applied` = 0 | code bit 0 = 1 |
| Re-arm | ramps up **from 0** | | |
| Armed, no RawCommand for 200 ms | ramps down to 0 at `dec` (stays armed) | `stale` | health WARNING (set by the app) |

RawCommand is accepted only from the host (node 2), and only if it has at least 8 entries. `ThrusterStatus.applied` carries all 8 values (17 bytes, a 3-frame transfer at 20 Hz).

### Changes from Orion `thrusters.c`
1. **Kill zeroes the ramp state.** Orion froze it, so after a kill the thruster stepped straight back to its old thrust on re-arm.
2. **`applied` reads 0 while killed.** Orion's readback kept showing the frozen value.
3. **`inc`/`dec` are magnitude-based.** `inc` spins up and `dec` spins down, in either direction. Orion's were signed (`inc` = towards +255). The 5/5 defaults behave identically.

The pulse map is Orion's `desToPWM` unchanged: 2 µs steps, 1500 µs neutral, a ±26 µs deadband, and 1100–1900 µs at full scale.

## Run it, put it on a G4

Both are now the sensor board's: `./build/sensor_linux`, `python tools/bench.py selftest mcast:0 30 --actuator --thruster`, and `tools/export_cubeide.sh sensor <project>`. CubeMX, PWM pins and the scope check are in `boards/sensor/README.md`.

## Not done yet
- **Parameters:** polarity, ramp period/inc/dec, and the cmd offset are `#define`s in `board_config.h` until stage 3. The new sub's polarities are not measured yet; Orion's two `polarities.h` files disagree on T5.
- **Sensor PCB pins:** the PWM table is still the Deft PCB bench one until the sensor PCB exists.
- **Bus load:** 8 thrusters make RawCommand 14 bytes (3 frames at 100 Hz = 300 frames/s) and ThrusterStatus 3 frames at 20 Hz. Together with the rest of the catalogue that is still well under 10 % of a 1 Mbit/s bus.
