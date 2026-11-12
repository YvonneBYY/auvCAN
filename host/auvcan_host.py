#!/usr/bin/env python3
"""
auvcan_host.py - the Jetson's side of auvCAN: node 2, the host.

    python host/auvcan_host.py --iface can0          # on the Jetson (SocketCAN, USB-CAN)
    python host/auvcan_host.py --iface mcast:0       # on the virtual bus
    python host/auvcan_host.py --iface slcan:/dev/cu.usbmodemXXXX   # through the gateway

What it does on the bus
  sends    cuauv.HostHeartbeat   10 Hz   alive + soft-kill request -> power board (node 1)
           esc.RawCommand        100 Hz  T1..T8 -> the thruster outputs on the sensor board (node 30)
           cuauv.RailCommand     1 Hz + on change, only once rails have been set
           RestartNode           on request
  receives everything the boards publish: Kill, NodeStatus (+ names via GetNodeInfo),
           ThrusterStatus, Depth, HullStatus, PowerStatus, RailCurrents, BatteryDetail,
           PingerBearing, AcousticsStatus

Safety rules (on top of the boards' own)
  - RawCommand carries the thrust setpoint only while the power board's Kill says armed
    and is fresh (< 250 ms); otherwise it carries zeros. So re-arming never jumps back
    to an old setpoint.
  - A thrust setpoint older than --setpoint-timeout (default 200 ms) is replaced by
    zeros: the control software must keep sending, like the thrusters' own 200 ms rule.
  - The heartbeat and RawCommand are sent from the same loop. If this process hangs,
    both stop, and the power board kills the sub within 500 ms.

Control interface for other software (any language): JSON over UDP on 127.0.0.1,
--control-port (default 47800, 0 = off). One datagram in, one reply out:
    {"cmd": "thrust", "values": [100, 0, 0, 0, 0, 0, 0, -50]}   Orion units, -255..255
    {"cmd": "soft_kill", "on": true}
    {"cmd": "rails", "enable_mask": 65535, "priv_mask": 0}
    {"cmd": "restart", "node": 30}
    {"cmd": "state"}                         -> the full state as JSON (see state())
In Python, import AuvcanHost and call the same methods directly.

  pip install dronecan
"""
import argparse
import json
import math
import os
import socket
import sys
import time
from collections import deque

import dronecan

HERE = os.path.dirname(os.path.abspath(__file__))
dronecan.load_dsdl(os.path.abspath(os.path.join(HERE, "..", "dsdl", "cuauv")))

T = dronecan.TYPENAMES
NodeStatus = dronecan.uavcan.protocol.NodeStatus
GetNodeInfo = dronecan.uavcan.protocol.GetNodeInfo
RestartNode = dronecan.uavcan.protocol.RestartNode
RawCommand = dronecan.uavcan.equipment.esc.RawCommand
Kill, HostHeartbeat = T["cuauv.Kill"], T["cuauv.HostHeartbeat"]
RailCommand = T["cuauv.RailCommand"]
TELEMETRY = {                       # short name -> type, stored per source node
    "thrusters": T["cuauv.ThrusterStatus"],
    "depth": T["cuauv.Depth"],
    "hull": T["cuauv.HullStatus"],
    "power": T["cuauv.PowerStatus"],
    "rail_currents": T["cuauv.RailCurrents"],
    "battery": T["cuauv.BatteryDetail"],
    "pinger": T["cuauv.PingerBearing"],
    "acoustics": T["cuauv.AcousticsStatus"],
}

HOST_ID, POWER_ID = 2, 1
THRUSTERS = 8
RAW_SCALE = 32                      # Orion units -> RawCommand int14
PRIO_ACTUATOR, PRIO_HOST_HB, PRIO_TELEMETRY = 4, 6, 12
HB_PERIOD, CMD_PERIOD, RAIL_PERIOD = 0.1, 0.01, 1.0
KILL_FRESH = 0.25                   # matches the thrusters' kill timeout
NODE_LOST = 3.0                     # NodeStatus is 1 Hz
HEALTH = {0: "OK", 1: "WARNING", 2: "ERROR", 3: "CRITICAL"}


def make_node(iface, node_id, name):
    """mcast:N, can0 (SocketCAN), or a serial SLCAN device (slcan:/dev/...[@BAUD])."""
    info = GetNodeInfo.Response()
    info.name = name
    kw = {"node_id": node_id, "node_info": info}
    if "@" in iface:
        iface, baud = iface.rsplit("@", 1)
        kw["baudrate"] = int(baud)
    if not iface.startswith("mcast"):
        kw["bitrate"] = 1000000
    return dronecan.make_node(iface, **kw)


def to_plain(v):
    """DSDL value -> JSON-friendly Python (NaN -> None, padding fields dropped)."""
    if isinstance(v, bool) or v is None or isinstance(v, (int, str)):
        return v
    if isinstance(v, float):
        return None if math.isnan(v) or math.isinf(v) else round(v, 4)
    if isinstance(v, (bytes, bytearray)):
        return v.decode(errors="replace")
    if hasattr(v, "_fields"):                       # compound
        return {k: to_plain(getattr(v, k)) for k in v._fields
                if type(getattr(v, k)).__name__ != "VoidValue"}
    if hasattr(v, "__len__"):                       # array
        return [to_plain(x) for x in v]
    if hasattr(v, "value"):                         # primitive wrapper
        return to_plain(v.value)
    return str(v)


class AuvcanHost:
    def __init__(self, iface, node_id=HOST_ID, setpoint_timeout=0.2, name="edu.cornell.cuauv.host"):
        self.node = make_node(iface, node_id, name)
        self.iface, self.setpoint_timeout = iface, setpoint_timeout
        self.t0 = time.monotonic()

        self.soft_kill = False
        self.thrust = [0] * THRUSTERS
        self.thrust_t = None                 # when the setpoint was last set
        self.rails = None                    # (enable_mask, priv_mask) once set
        self.hb_seq = self.rail_seq = 0
        self.sent_thrust = [0] * THRUSTERS   # what the last RawCommand carried

        self.kill, self.kill_t = None, None
        self.nodes = {}                      # id -> {ns, t, name}
        self.telemetry = {}                  # (kind, source) -> (msg, t)
        self.events = deque(maxlen=100)
        self.event_count = 0                 # total ever logged (the deque keeps the last 100)
        self._last_armed = None
        self._last_leak = None

        self.node.add_handler(NodeStatus, self._on_ns)
        self.node.add_handler(Kill, self._on_kill)
        for kind, typ in TELEMETRY.items():
            self.node.add_handler(typ, lambda e, k=kind: self._on_tel(k, e))
        self.node.periodic(HB_PERIOD, self._send_heartbeat)
        self.node.periodic(CMD_PERIOD, self._send_rawcommand)
        self.node.periodic(RAIL_PERIOD, self._send_rails)
        self.node.periodic(1.0, self._housekeeping)

    # ---- commands --------------------------------------------------------------
    def set_thrust(self, values):
        vals = [int(v) for v in values]
        if len(vals) > THRUSTERS or not all(-255 <= v <= 255 for v in vals):
            raise ValueError("thrust: up to %d values, each -255..255 (Orion units)" % THRUSTERS)
        self.thrust = vals + [0] * (THRUSTERS - len(vals))
        self.thrust_t = time.monotonic()

    def set_soft_kill(self, on):
        if bool(on) != self.soft_kill:
            self.event("soft kill %s" % ("ON" if on else "off"))
        self.soft_kill = bool(on)
        self._send_heartbeat()               # don't wait up to 100 ms

    def set_rails(self, enable_mask, priv_mask=0):
        new = (int(enable_mask) & 0xFFFF, int(priv_mask) & 0xFFFF)
        changed = new != self.rails
        self.rails = new
        if changed:
            self._send_rails()

    def restart(self, node_id, callback=None):
        req = RestartNode.Request(magic_number=RestartNode.Request().MAGIC_NUMBER)
        def done(e):
            self.event("RestartNode %d: %s" % (node_id, "no response" if e is None else "ok=%s" % bool(e.response.ok)))
            if callback:
                callback(e)
        self.node.request(req, node_id, done, timeout=1.0)

    # ---- state -----------------------------------------------------------------
    def armed(self, now=None):
        now = time.monotonic() if now is None else now
        return self.kill is not None and now - self.kill_t < KILL_FRESH and bool(self.kill.armed)

    def latest(self, kind, source=None):
        """Latest message of a telemetry kind (from `source`, or from any node)."""
        best = None
        for (k, src), (msg, t) in self.telemetry.items():
            if k == kind and (source is None or src == source) and (best is None or t > best[1]):
                best = (msg, t)
        return None if best is None else best[0]

    def state(self):
        now = time.monotonic()
        k = self.kill
        out = {
            "t": round(now - self.t0, 3),
            "iface": self.iface,
            "armed": self.armed(now),
            "kill": None if k is None else {
                "armed": bool(k.armed), "hard_kill": bool(k.hard_kill), "soft_kill": bool(k.soft_kill),
                "host_ok": bool(k.host_ok), "node_kill": bool(k.node_kill),
                "node_kill_src": int(k.node_kill_src), "age_s": round(now - self.kill_t, 3)},
            "soft_kill_requested": self.soft_kill,
            "thrust_setpoint": self.thrust,
            "thrust_setpoint_fresh": self._setpoint_fresh(now),
            "thrust_sent": self.sent_thrust,
            "rails": None if self.rails is None else {"enable_mask": self.rails[0], "priv_mask": self.rails[1]},
            "nodes": {str(nid): {"name": n["name"], "health": HEALTH.get(n["ns"].health, n["ns"].health),
                                  "status_code": int(n["ns"].vendor_specific_status_code),
                                  "uptime_s": int(n["ns"].uptime_sec), "online": now - n["t"] < NODE_LOST}
                      for nid, n in sorted(self.nodes.items())},
            "telemetry": {},
            "events": list(self.events)[-20:],
        }
        for (kind, src), (msg, t) in sorted(self.telemetry.items()):
            out["telemetry"]["%s/%d" % (kind, src)] = dict(to_plain(msg), age_s=round(now - t, 3))
        return out

    def event(self, text):
        self.events.append("%.3f %s" % (time.monotonic() - self.t0, text))
        self.event_count += 1

    def spin(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.node.spin(0.005)

    # ---- bus handlers ------------------------------------------------------------
    def _on_ns(self, e):
        nid = e.transfer.source_node_id
        n = self.nodes.get(nid)
        if n is None:
            n = self.nodes[nid] = {"name": None, "ns": e.message, "t": time.monotonic(), "asked": 0.0, "lost": False}
            self.event("node %d online" % nid)
        elif n["lost"]:
            self.event("node %d back" % nid)
        n["ns"], n["t"], n["lost"] = e.message, time.monotonic(), False

    def _on_kill(self, e):
        if e.transfer.source_node_id != POWER_ID:
            self.event("Kill from node %d ignored: only node %d is the kill authority" % (e.transfer.source_node_id, POWER_ID))
            return
        self.kill, self.kill_t = e.message, time.monotonic()
        armed = bool(e.message.armed)
        if armed != self._last_armed:
            why = [w for w, on in (("hard", e.message.hard_kill), ("soft", e.message.soft_kill),
                                   ("no host", not e.message.host_ok),
                                   ("node %d" % e.message.node_kill_src, e.message.node_kill)) if on]
            self.event("ARMED" if armed else "KILLED (%s)" % ", ".join(why or ["?"]))
            self._last_armed = armed

    def _on_tel(self, kind, e):
        self.telemetry[(kind, e.transfer.source_node_id)] = (e.message, time.monotonic())
        if kind == "hull":
            leak = [bool(v) for v in e.message.leak]
            if self._last_leak is not None and leak != self._last_leak:
                on = [str(i + 1) for i, v in enumerate(leak) if v]
                self.event("LEAK %s (priority %d)" % (",".join(on) if on else "cleared", e.transfer.transfer_priority))
            self._last_leak = leak

    # ---- periodic senders --------------------------------------------------------
    def _setpoint_fresh(self, now):
        return self.thrust_t is not None and now - self.thrust_t < self.setpoint_timeout

    def _send_heartbeat(self):
        self.node.broadcast(HostHeartbeat(soft_kill=self.soft_kill, seq=self.hb_seq & 0xFF), priority=PRIO_HOST_HB)
        self.hb_seq += 1

    def _send_rawcommand(self):
        now = time.monotonic()
        vals = self.thrust if (self.armed(now) and self._setpoint_fresh(now)) else [0] * THRUSTERS
        self.sent_thrust = list(vals)
        raw = [max(-8192, min(8191, v * RAW_SCALE)) for v in vals]
        self.node.broadcast(RawCommand(cmd=raw), priority=PRIO_ACTUATOR)

    def _send_rails(self):
        if self.rails is None:
            return                           # never override the power board's own state unasked
        self.node.broadcast(RailCommand(enable_mask=self.rails[0], priv_mask=self.rails[1],
                                        seq=self.rail_seq & 0xFF), priority=PRIO_TELEMETRY)
        self.rail_seq += 1

    def _housekeeping(self):
        now = time.monotonic()
        for nid, n in self.nodes.items():
            if not n["lost"] and now - n["t"] > NODE_LOST:
                n["lost"] = True
                self.event("node %d LOST" % nid)
            if n["name"] is None and nid != self.node.node_id and now - n["asked"] > 2.0 and not n["lost"]:
                n["asked"] = now
                self.node.request(GetNodeInfo.Request(), nid,
                                  lambda e, nid=nid: self._on_info(nid, e), timeout=1.0)

    def _on_info(self, nid, e):
        if e is not None and nid in self.nodes:
            name = e.response.name
            self.nodes[nid]["name"] = name.decode() if isinstance(name, (bytes, bytearray)) else str(name)


# ---------------------------------------------------------------------------------
class ControlPort:
    """JSON-over-UDP control on 127.0.0.1 (see the module docstring)."""
    def __init__(self, host, port):
        self.host = host
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", port))
        self.sock.setblocking(False)

    def poll(self):
        while True:
            try:
                data, addr = self.sock.recvfrom(65536)
            except (BlockingIOError, InterruptedError):
                return
            # Whatever a client sends, the host must keep running: if it stopped, the
            # heartbeat would stop and the power board would kill the sub.
            try:
                out = json.dumps(self.handle(json.loads(data.decode())))
            except Exception as ex:
                out = json.dumps({"ok": False, "error": "%s: %s" % (type(ex).__name__, ex)})
            try:
                self.sock.sendto(out.encode(), addr)
            except OSError:
                pass

    def handle(self, req):
        cmd = req.get("cmd")
        if cmd == "thrust":
            self.host.set_thrust(req["values"])
        elif cmd == "soft_kill":
            self.host.set_soft_kill(bool(req["on"]))
        elif cmd == "rails":
            self.host.set_rails(req["enable_mask"], req.get("priv_mask", 0))
        elif cmd == "restart":
            self.host.restart(int(req["node"]))
        elif cmd == "state":
            return {"ok": True, "state": self.host.state()}
        else:
            return {"ok": False, "error": "unknown cmd %r" % cmd}
        return {"ok": True}


def summary(h):
    s = h.state()
    k = s["kill"]
    kill = "no Kill" if k is None else ("ARMED" if k["armed"] else "KILLED") + \
        "".join(" " + w for w, on in (("hard", k["hard_kill"]), ("soft", k["soft_kill"]),
                                       ("no-host", not k["host_ok"]), ("node%d" % k["node_kill_src"], k["node_kill"])) if on)
    online = [n for n, v in s["nodes"].items() if v["online"]]
    line = "[host %7.1fs] %-24s nodes %-18s sent %s" % (s["t"], kill, ",".join(online), s["thrust_sent"])
    d = h.latest("depth")
    if d is not None:
        line += " | depth %s" % ("%.2f m" % d.depth_m if d.sensor_ok else "FAULT")
    hl = h.latest("hull")
    if hl is not None and any(hl.leak):
        line += " | LEAK " + ",".join(str(i + 1) for i, v in enumerate(hl.leak) if v)
    return line


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--iface", default="mcast:0")
    p.add_argument("--node-id", type=int, default=HOST_ID)
    p.add_argument("--control-port", type=int, default=47800, help="JSON/UDP control on 127.0.0.1 (0 = off)")
    p.add_argument("--setpoint-timeout", type=float, default=0.2)
    p.add_argument("--quiet", action="store_true", help="no 1 Hz summary line")
    a = p.parse_args()

    host = AuvcanHost(a.iface, a.node_id, a.setpoint_timeout)
    ctl = ControlPort(host, a.control_port) if a.control_port else None
    print("auvCAN host: node %d on %s%s (Ctrl-C to stop)" % (a.node_id, a.iface,
          ", control on udp://127.0.0.1:%d" % a.control_port if ctl else ""), flush=True)
    next_print, printed = time.monotonic() + 1.0, 0
    try:
        while True:
            host.node.spin(0.002)
            if ctl:
                ctl.poll()
            if not a.quiet:
                new = min(host.event_count - printed, len(host.events))
                for line in list(host.events)[len(host.events) - new:] if new > 0 else []:
                    print("[host event] " + line, flush=True)
                printed = host.event_count
                if time.monotonic() >= next_print:
                    next_print += 1.0
                    print(summary(host), flush=True)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
