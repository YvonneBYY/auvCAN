# Host (Jetson, node 2)

`auvcan_host.py` is the Jetson's side of auvCAN. It is the only node whose `RawCommand` the thruster outputs (on the sensor board, node 30) obey, and its `HostHeartbeat` is one of the conditions the power board needs before it arms.

```bash
pip install dronecan
python host/auvcan_host.py --iface can0                      # on the Jetson
python host/auvcan_host.py --iface mcast:0                   # on the virtual bus
python host/auvcan_host.py --iface slcan:/dev/cu.usbmodemXXXX   # through a USB bridge
```

It prints a summary line once a second and an event line when something changes (armed/killed and why, a node coming online or going quiet, a leak).

## On the bus

| Sends | Rate | To |
|---|---|---|
| `cuauv.HostHeartbeat` | 10 Hz | power board: "host alive" + soft-kill request |
| `esc.RawCommand` (T1–T8) | 100 Hz | sensor board (node 30), which drives the ESCs |
| `cuauv.RailCommand` | 1 Hz, only once rails have been set | power board |
| `RestartNode` | on request | any board |

It receives everything the boards publish (Kill, NodeStatus + names, ThrusterStatus, Depth, HullStatus, PowerStatus, RailCurrents, BatteryDetail, PingerBearing, AcousticsStatus) and keeps the latest of each with its age.

## Safety rules (on top of the boards' own)

- `RawCommand` carries the setpoint **only while the power board's Kill says armed and is fresh** (< 250 ms). Otherwise it sends zeros, so re-arming never jumps to an old setpoint.
- A setpoint older than `--setpoint-timeout` (200 ms) becomes zeros: the control software has to keep sending.
- Heartbeat and RawCommand come from the same loop. If this process hangs, both stop and the power board kills the sub within 500 ms.

## Talking to it from other software

JSON over UDP on `127.0.0.1:47800` (`--control-port`, 0 = off). One datagram in, one reply out; a bad request gets `{"ok": false, "error": "..."}` and the host keeps running.

```json
{"cmd": "thrust", "values": [100, 0, 0, 0, 0, 0, 0, -50]}
{"cmd": "soft_kill", "on": true}
{"cmd": "rails", "enable_mask": 65535, "priv_mask": 0}
{"cmd": "restart", "node": 30}
{"cmd": "state"}
```

Thrust is in Orion units (−255..255), T1..T8. `state` returns the node table, kill state, latest telemetry per board (`thrusters/30`, `depth/30`, `hull/30`, …, each with `age_s`) and recent events.

From Python, import it instead:

```python
from auvcan_host import AuvcanHost
h = AuvcanHost("can0")
h.set_thrust([100, 0, 0, 0, 0, 0, 0, -50])
h.spin(0.05)            # call often; it does all the sending
print(h.armed(), h.latest("depth", 30))
```

## Testing it

`tools/system_test.py` runs it against every board on a private virtual bus. See the top-level README.
