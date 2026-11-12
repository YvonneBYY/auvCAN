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

## Changes from Orion
1. **MS5837 at OSR 4096, non-blocking.** Orion used OSR 256 (the noisiest) and then blocked 2 × 20 ms per reading (about 25 Hz). Here both conversions fit one 20 ms period, giving 50 Hz.
2. **MS5837 variant is explicit.** Orion's formulas are the **02BA** set, which matches its datasheet example exactly. A **30BA (Bar30)** with those formulas gives a badly wrong pressure; set `SENSOR_MS5837_MODEL MS5837_30BA`. The PROM CRC is now checked.
3. **MCP9808 below 0 °C.** Orion copied the datasheet's sample code, which returns −0.25 °C as +0.25 °C. Fixed (13-bit two's complement).
4. **Leak debounce.** A change must hold for 3 samples 10 ms apart. Orion's README mentions the leak code "caused some issues at first".
5. **Leak events go out at once**, at priority 2, instead of waiting for the next 100 ms serial poll.
6. **Hull pressure**: same formula and same SPI framing (one junk byte, then the 16-bit result MSB first). **Check that framing against the ADC's datasheet**: many 16-bit SPI ADCs don't put the result on byte boundaries.

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

## Put it on a G4 (Deft Controls PCB as the stand-in)

1. **CubeMX**, on top of the bench setup (FDCAN with "interrupt 0", SysTick timebase, USB CDC for the status line if you use it):
   - **I2C3**: I2C mode, Fast mode 400 kHz, on **PC8 (SCL) / PC9 (SDA)**.
   - **SPI1**: Full-Duplex Master, 8-bit, mode 0 (CPOL 0, CPHA 0) as in Orion, a baud rate of a few MHz or less, on **PA5 (SCK) / PA6 (MISO) / PA7 (MOSI)**. NSS = software (CS is PA4, driven by `hw_g4.c`).
   - CS (PA4) and the leak pins (**PC4, PC5, PB0, PB1**) are configured by `hw_g4.c`; leave them unassigned in CubeMX.
   - Other pins: edit `board_config.h` (`BOARD_SENSOR_I2C`, `BOARD_SENSOR_SPI`, `BOARD_ADC_CS_*`, `BOARD_LEAK_PINS`).
   - **8 PWM outputs** on three timers, each channel "PWM Generation CHx" (`pwm_g4.c` sets prescaler and period itself, so CubeMX's values don't matter):

     | Thruster | Timer / channel | Pin |
     |---|---|---|
     | T1–T3 | TIM1 CH1–3 | PC0, PA9, PA10 |
     | T4–T7 | TIM2 CH1–4 | PA0, PA1, PA2, PA3 |
     | T8 | **TIM3 CH1** | **PC6** (was PA6 on the old thruster board; PA6 is SPI1 MISO here) |

     Check PC6 is broken out on your Deft board; otherwise pick another free timer channel and edit `BOARD_PWM_OUTPUTS` / `BOARD_PWM_TIMERS`. All listed timers must run from the same clock (`BOARD_PWM_TIM_CLOCK_HZ`, 170 MHz in the bench tree).
2. **`board_config.h`**: `SENSOR_MS5837_MODEL` (02BA vs 30BA), `SENSOR_WATER_DENSITY`, leak polarity (`BOARD_LEAK_ACTIVE_HIGH`, default high = leak as on Orion).
3. **Export**: `sh tools/export_cubeide.sh sensor <project>` (copies the thruster files too), include paths as printed; `BOARD_DEFT_CAN_CH=2` if CAN is on CH2.
4. **`main.c`**: `app_setup();` in USER CODE BEGIN 2, `app_loop();` in `while (1)`.

### Wiring the breakouts
- **I2C**: MS5837 and MCP9808 on the same two wires, 3.3 V supply, **pull-ups 2.2–4.7 kΩ to 3.3 V** on SCL and SDA (once, not per breakout; many breakouts already have them). MCP9808 A0–A2 to GND for 0x18.
- **SPI ADC**: SCK, MISO, MOSI (if the ADC has an input), CS → PA4, common GND, and the ADC's reference the same as the pressure sensor's supply (the conversion is ratiometric).
- **Leaks**: each leak sensor output to its pin; an unconnected pin reads "no leak" (internal pull-down).

### Bring-up checks (USB status line or `bench.py sensors`)
- `depth … m water … C`: in air, depth reads about 0 (± a few cm with the weather). Pressing a finger on the sensor's membrane, or a syringe, raises it.
- `i2c err` stays at 0. If it climbs: wiring, pull-ups, address. `crc err` > 0: a PROM read is corrupted (bus noise).
- `early` counts MS5837 reads that came before the conversion finished; the board restarts that conversion (3 tries, then it counts as a failure). An occasional one is harmless; a steadily climbing count means the loop is being held up for milliseconds somewhere.
- `int … C` close to room temperature.
- `hull … atm` about 1.00 with the hull open. `hull FAULT (raw 65535)` means MISO is floating (no ADC); `raw 0` means it's tied low.
- Short a leak pin to 3.3 V: `leak 1000` within about 30 ms, and `bench.py sensors` prints a `LEAK CHANGE … (priority 2)` line.

### Scope the PWM before connecting an ESC
- At boot, all eight pins show 1500 µs high every 2 ms.
- Arm (`bench.py kill <iface> --armed --bench`) and command T1 full (`bench.py thrust <iface> 255 --bench`): the pulse ramps to 1900 µs over about 0.5 s.
- Kill: back to 1500 µs within one period. The sensor readings keep coming throughout.

## Not done yet
- **Pressure ADC part**: confirm its SPI framing and mode against its datasheet (see change 6).
- **Parameters**: model, water density, surface pressure and leak polarity are `#define`s until stage 3. A "tare depth at the surface" command would replace the fixed 101325 Pa.
- **Sensor PCB pins**: the pin-out (sensors and PWM) is the Deft bench one until the sensor PCB exists.
- **Thruster polarity** of all 8 still to be measured.
