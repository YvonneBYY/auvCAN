# cuauv DSDL namespace

Reserved data type ID block: **20600-20699** (messages).

Chosen because the "vendor-specific" range 20000-21000 is already used in
places by the ArduPilot, com.*, cuav and mppt namespaces that ship with
pydronecan and the DroneCAN GUI Tool; tools refuse to load two types with the
same ID. 20600-20699 is unused by all of them (checked against pydronecan 1.0.27
and dronecan/DSDL master). Re-check before adding IDs outside this block.

| ID    | Type                    | Publisher            | Rate                 | Prio |
|-------|-------------------------|----------------------|----------------------|------|
| 20600 | cuauv.Kill              | Controller (1)       | 20 Hz + on change    | 0    |
| 20601 | cuauv.HostHeartbeat     | Host (2)             | 10 Hz                | 6    |
| 20602 | cuauv.KillRequest       | BMB (20), Power (21) | 2 Hz while active    | 1    |
| 20610 | cuauv.ThrusterStatus    | Thruster A/B (10/11) | 20 Hz                | 12   |
| 20620 | cuauv.Depth             | Sensor (30)          | 50 Hz                | 8    |
| 20621 | cuauv.HullStatus        | Sensor (30)          | 10 Hz + on leak      | 12/2 |
| 20630 | cuauv.PowerStatus       | Power (21)           | 10 Hz                | 12   |
| 20631 | cuauv.RailCurrents      | Power (21)           | 10 Hz                | 14   |
| 20632 | cuauv.RailCommand       | Host (2)             | 1 Hz + on change     | 12   |
| 20640 | cuauv.BatteryDetail     | BMB (20)             | 1 Hz                 | 14   |
| 20650 | cuauv.PingerBearing     | Acoustics (31)       | per ping, <= 10 Hz   | 12   |
| 20651 | cuauv.AcousticsStatus   | Acoustics (31)       | 1 Hz                 | 14   |
| 20652 | cuauv.AcousticsCommand  | Host (2)             | 1 Hz + on change     | 12   |

Frame counts are locked by `tests/dsdl_sizes.c`; a field change that adds a frame fails the build.

Regenerate C code after any change:

    # needs: pip install empy==3.3.4 pexpect (in a venv)
    python3 third_party/dronecan_dsdlc/dronecan_dsdlc.py -O generated \
        third_party/dronecan_dsdl/uavcan dsdl/cuauv
