# Sensor board

Node 30. Two jobs on one board:
- **Sensors:** reads depth, internal temperature, hull pressure and four leak inputs, and publishes them. Same parts as Orion's sensor board (`Orion/Sensor/sensor.c`).
- **Thrusters:** drives the **8 ESC PWM outputs** (T1–T8) from the host's `RawCommand`, obeying `cuauv.Kill` from the power board. This used to be a separate thruster board (node 10, now retired); the logic is `boards/thruster/thruster.c`, unchanged.

The two are independent: a kill stops the thrusters, never the sensors, and a failed sensor never touches the thrusters.


| Measurement | Part | Bus |
|---|---|---|
| Depth + water temperature | MS5837 (02BA, or 30BA = Blue Robotics Bar30) | I2C, 0x76 |
| Internal temperature | MCP9808-E/MS | I2C, 0x18 |
| Hull pressure | analog pressure sensor on an external 16-bit ADC | SPI |
| Leaks ×4 | digital inputs | GPIO |

| File | What |
|---|---|
| `sensor.c/.h` | All the board logic: MS5837 state machine and compensation maths, MCP9808 and pressure conversions, leak debounce, publishing. No HAL. |
| `sim_chips.c/.h` | Software models of the chips (real I2C/SPI protocol), for the tests and the Linux build |
| `app.c` | Wires `sensor.c` **and** `boards/thruster/thruster.c` to the auvCAN core and the BSP; NodeStatus health; status line. Same file on G4 and Linux. |
| `board_config.h` | Node ID, MS5837 variant, water density, I2C/SPI handles, CS and leak pins; thruster polarity, ramp, command source and the 8-output PWM table |
| `hw_g4.c` / `hw_linux.c` | Sensor I/O: HAL I2C + SPI + GPIO / simulated chips |
| `boards/thruster/pwm_g4.c` / `pwm_linux.c` | ESC outputs: G4 timers / printed pulse widths |
| `tests/test_sensor.c` | 84 checks: datasheet vectors, CRC, conversions, rates, leak events, faults and recovery |

## What it sends

| Message | When | Priority | Contents |
|---|---|---|---|
| `cuauv.Depth` | 50 Hz | 8 (`SENSOR_FAST`) | `depth_m`, `water_temp_c`, `sensor_ok` |
| `cuauv.HullStatus` | 10 Hz | 14 (`TELEM_SLOW`) | `hull_pressure_atm`, `internal_temp_c`, `leak[4]`, `pressure_sensor_ok` |
| `cuauv.HullStatus` | **immediately on a leak change** | **2** (`HULL_EVENT`) | same |
| `cuauv.ThrusterStatus` | 20 Hz | 12 (`TELEMETRY`) | `applied[8]`, `killed`, `stale` |
| NodeStatus | 1 Hz | | status code: **bit 0 killed**, bits 1–4 leaks 1–4, bit 5 depth fault, bit 6 internal-temp fault, bit 7 hull-pressure fault. Health WARNING while a sensor has failed or the thrusters are armed with no commands arriving |

| It receives | From | |
|---|---|---|
| `cuauv.Kill` | node 1 only | no Kill for 250 ms = killed: outputs to 1500 µs at once |
| `esc.RawCommand` | node 2 only (`THRUSTER_CMD_SOURCE`; 1 for a bench-throttle build) | T1–T8 = `cmd[0..7]`; no command for 200 ms = ramp to 0 |

A leak is report-only: the Jetson decides what to do. Thruster behaviour (ramp, kill, re-arm from 0, stale) is in `boards/thruster/README.md`.

The main loop runs the thrusters first, then the sensors. I2C transfers block for at most 5 ms each (`hw_g4.c`), so a stuck I2C bus can delay a kill edge by a few transfers' worth (worst case about 15–20 ms); the 250 ms Kill timeout is unaffected.

A failed sensor publishes **NaN** and clears its `*_ok` flag, while the 50 Hz / 10 Hz rates continue. The MS5837 and MCP9808 are re-initialised once a second until they answer, so a breakout plugged back in recovers on its own.

LED: on = all sensors OK; 2 Hz blink = a leak input is active; off = a sensor has failed. The heartbeat LED shows the firmware is running. Armed / killed is on the status line and in ThrusterStatus.

## Run it on the virtual bus

```bash
./build/sensor_linux                       # depth swings 0-2 m over 30 s; prints the 8 pulses too
AUVCAN_SENSOR_DEPTH=1.5 ./build/sensor_linux   # or a fixed depth
echo 0100 > /tmp/auvcan_leak               # leak on input 2; echo 0000 to clear
python tools/bench.py sensors mcast:0      # live readings + leak events with their priority
python tools/bench.py selftest mcast:0 30 --actuator --thruster   # the thruster checks
```
`sensor_bench_linux` is the same board obeying node 1's bench throttle instead of the host.

`sub.py` and `virtual_sub.sh` start it as node 30.
