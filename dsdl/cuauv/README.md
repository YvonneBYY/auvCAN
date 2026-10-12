# cuauv DSDL namespace

Reserved data type ID block: **20600-20699** (messages).

Chosen because the "vendor-specific" range 20000-21000 is already used in
places by the ArduPilot, com.*, cuav and mppt namespaces that ship with
pydronecan and the DroneCAN GUI Tool; tools refuse to load two types with the
same ID. 20600-20699 is unused by all of them (checked against pydronecan 1.0.27
and dronecan/DSDL master). Re-check before adding IDs outside this block.

| ID    | Type               | Publisher            | Rate  | Prio |
|-------|--------------------|----------------------|-------|------|
| 20600 | cuauv.Kill         | controller (node 1)  | 20 Hz | 0    |
| 20601 | cuauv.HostHeartbeat| Jetson host (node 2) | 10 Hz | 6    |

Regenerate C code after any change:

    python3 third_party/dronecan_dsdlc/dronecan_dsdlc.py -O generated \
        third_party/dronecan_dsdl/uavcan dsdl/cuauv
