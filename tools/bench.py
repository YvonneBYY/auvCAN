#!/usr/bin/env python3
"""
bench.py - auvCAN bench tool (pydronecan).

Interfaces:
  can0           SocketCAN (CANable/candleLight via gs_usb, or the controller board)
  /dev/ttyACM0   SLCAN adapter
  mcast:0        virtual bus shared with build/virtual_node and the DroneCAN GUI Tool

Commands:
  monitor IFACE                 live table of every node (NodeStatus + GetNodeInfo)
  kill IFACE --armed|--killed   act as the controller (node 1): publish cuauv.Kill at 20 Hz
  host IFACE [--soft-kill]      act as the Jetson (node 2): publish cuauv.HostHeartbeat at 10 Hz
  restart IFACE NODE            send RestartNode to NODE
  thrust IFACE V1 [V2 .. V6]    act as the Jetson (node 2): RawCommand at 100 Hz (Orion units,
                                -255..255, sent x32) + HostHeartbeat; prints ThrusterStatus
  selftest IFACE NODE [--actuator] [--thruster]
                                automated stage-2/stage-4 bring-up checks against NODE;
                                --thruster adds stage 5 (ramp, kill, re-arm, stale) for node 10/11

Only use `kill` / `selftest --actuator|--thruster` on a bench WITHOUT a real controller
board: they act as node 1. `thrust` and `selftest --thruster` also act as node 2 (the host).

  pip install dronecan
"""
import argparse
import os
import sys
import time

import dronecan

HERE = os.path.dirname(os.path.abspath(__file__))
dronecan.load_dsdl(os.path.abspath(os.path.join(HERE, "..", "dsdl", "cuauv")))

NodeStatus = dronecan.uavcan.protocol.NodeStatus
GetNodeInfo = dronecan.uavcan.protocol.GetNodeInfo
RestartNode = dronecan.uavcan.protocol.RestartNode
Kill = dronecan.TYPENAMES["cuauv.Kill"]   # custom namespaces are registered by name only
HostHeartbeat = dronecan.TYPENAMES["cuauv.HostHeartbeat"]
ThrusterStatus = dronecan.TYPENAMES["cuauv.ThrusterStatus"]
RawCommand = dronecan.uavcan.equipment.esc.RawCommand

CONTROLLER_ID = 1
HOST_ID = 2
MONITOR_ID = 126
HOST_HB_PERIOD = 0.1
PRIO_HOST_HB = 6
KILL_PERIOD = 0.05
PRIO_KILL = 0
RAWCMD_PERIOD = 0.01
PRIO_ACTUATOR = 4
RAW_SCALE = 32
THRUSTER_SLICE = {10: 0, 11: 3}     # node -> first index in RawCommand.cmd
HEALTH = {0: "OK", 1: "WARNING", 2: "ERROR", 3: "CRITICAL"}
MODE = {0: "OPERATIONAL", 1: "INIT", 2: "MAINT", 3: "SW_UPDATE", 7: "OFFLINE"}


def make_node(iface, node_id):
    kw = {"node_id": node_id}
    if not iface.startswith("mcast"):
        kw["bitrate"] = 1000000
    return dronecan.make_node(iface, **kw)


def spin_for(node, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        node.spin(0.01)


def get_node_info(node, target, timeout=1.0):
    box = {}
    node.request(GetNodeInfo.Request(), target, lambda e: box.setdefault("r", e), timeout=timeout)
    end = time.monotonic() + timeout + 0.2
    while "r" not in box and time.monotonic() < end:
        node.spin(0.01)
    e = box.get("r")
    return None if e is None else e.response


class KillPublisher:
    def __init__(self, node, armed=False):
        self.node, self.armed, self.seq, self.enabled = node, armed, 0, True
        node.periodic(KILL_PERIOD, self._tick)

    def _tick(self):
        if not self.enabled:
            return
        self.node.broadcast(Kill(armed=self.armed, hard_kill=False, soft_kill=not self.armed,
                                 host_ok=True, seq=self.seq), priority=PRIO_KILL)
        self.seq = (self.seq + 1) & 0xFF


class CommandPublisher:
    """Acts as the host: RawCommand at 100 Hz and HostHeartbeat at 10 Hz."""
    def __init__(self, node, values):
        self.node, self.values, self.enabled, self.seq = node, list(values), True, 0
        node.periodic(RAWCMD_PERIOD, self._cmd)
        node.periodic(HOST_HB_PERIOD, self._hb)

    def _cmd(self):
        if self.enabled:
            raw = [max(-8192, min(8191, int(v) * RAW_SCALE)) for v in self.values]
            self.node.broadcast(RawCommand(cmd=raw), priority=PRIO_ACTUATOR)

    def _hb(self):
        self.node.broadcast(HostHeartbeat(soft_kill=False, seq=self.seq), priority=PRIO_HOST_HB)
        self.seq = (self.seq + 1) & 0xFF


# ---------------------------------------------------------------------------

def cmd_monitor(a):
    node = make_node(a.iface, MONITOR_ID)
    nodes, names = {}, {}
    node.add_handler(NodeStatus, lambda e: nodes.__setitem__(e.transfer.source_node_id, (time.monotonic(), e.message)))
    while True:
        spin_for(node, 1.0)
        for nid in sorted(nodes):
            if nid not in names:
                info = get_node_info(node, nid, 0.5)
                if info is not None:
                    names[nid] = info.name.decode() if isinstance(info.name, (bytes, bytearray)) else str(info.name)
        print("\033[2J\033[H%-4s %-34s %-8s %-12s %8s %6s %s" % ("ID", "NAME", "HEALTH", "MODE", "UPTIME", "CODE", "AGE"))
        now = time.monotonic()
        for nid in sorted(nodes):
            t, m = nodes[nid]
            print("%-4d %-34s %-8s %-12s %7ds %6d %4.1fs" % (
                nid, names.get(nid, "?"), HEALTH.get(m.health, m.health), MODE.get(m.mode, m.mode),
                m.uptime_sec, m.vendor_specific_status_code, now - t))


def cmd_kill(a):
    node = make_node(a.iface, CONTROLLER_ID)
    KillPublisher(node, armed=a.armed)
    print("publishing cuauv.Kill armed=%s at 20 Hz as node 1 (Ctrl-C to stop)" % a.armed)
    end = time.monotonic() + a.seconds if a.seconds else None
    while end is None or time.monotonic() < end:
        node.spin(0.01)


def cmd_host(a):
    node = make_node(a.iface, HOST_ID)
    state = {"seq": 0}

    def tick():
        node.broadcast(HostHeartbeat(soft_kill=a.soft_kill, seq=state["seq"]), priority=PRIO_HOST_HB)
        state["seq"] = (state["seq"] + 1) & 0xFF

    node.periodic(HOST_HB_PERIOD, tick)
    print("publishing cuauv.HostHeartbeat soft_kill=%s at 10 Hz as node 2 (Ctrl-C to stop)" % a.soft_kill)
    end = time.monotonic() + a.seconds if a.seconds else None
    while end is None or time.monotonic() < end:
        node.spin(0.01)


def cmd_thrust(a):
    if not all(-255 <= v <= 255 for v in a.values):
        print("values are Orion units, -255..255")
        return 2
    values = list(a.values) + [0] * (6 - len(a.values))
    node = make_node(a.iface, HOST_ID)
    CommandPublisher(node, values)
    last = {}
    node.add_handler(ThrusterStatus, lambda e: last.__setitem__(e.transfer.source_node_id, e.message))
    print("RawCommand %s (x%d) at 100 Hz as node 2; needs Kill(armed) from node 1 to move "
          "(Ctrl-C to stop)" % (values, RAW_SCALE))
    end = time.monotonic() + a.seconds if a.seconds else None
    next_print = time.monotonic()
    while end is None or time.monotonic() < end:
        node.spin(0.01)
        if time.monotonic() >= next_print:
            next_print += 0.5
            for nid in sorted(last):
                m = last[nid]
                print("  node %d applied %s%s%s" % (nid, list(m.applied),
                      " KILLED" if m.killed else "", " STALE" if m.stale else ""))


def cmd_restart(a):
    node = make_node(a.iface, MONITOR_ID)
    box = {}
    node.request(RestartNode.Request(magic_number=RestartNode.Request().MAGIC_NUMBER), a.node,
                 lambda e: box.setdefault("r", e), timeout=1.0)
    spin_for(node, 1.2)
    e = box.get("r")
    print("no response" if e is None else "ok=%s" % bool(e.response.ok))
    return 0 if e is not None and e.response.ok else 1


def cmd_selftest(a):
    # Only act as node 1 when we must publish Kill ourselves (--actuator on a bench
    # with no controller board). Otherwise stay out of the controller's way.
    if a.thruster and a.node not in THRUSTER_SLICE:
        print("--thruster needs node 10 or 11")
        return 2
    node = make_node(a.iface, CONTROLLER_ID if (a.actuator or a.thruster) else MONITOR_ID)
    host = make_node(a.iface, HOST_ID) if a.thruster else None
    status = {}
    node.add_handler(NodeStatus, lambda e: status.__setitem__(e.transfer.source_node_id, e.message)
                     if e.transfer.source_node_id == a.node else None)
    results = []

    def check(name, ok, detail=""):
        results.append(ok)
        print("  [%s] %s %s" % ("PASS" if ok else "FAIL", name, detail))

    def spin():
        node.spin(0.005)
        if host is not None:
            host.spin(0.005)

    def wait_for(pred, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            spin()
            if pred():
                return True
        return False

    print("stage 2: node services")
    check("NodeStatus received", wait_for(lambda: a.node in status, 2.5))
    if a.node in status:
        m = status[a.node]
        check("health OK", m.health == 0, "(health=%d)" % m.health)
    info = get_node_info(node, a.node)
    check("GetNodeInfo answered", info is not None)
    if info is not None:
        name = info.name.decode() if isinstance(info.name, (bytes, bytearray)) else str(info.name)
        uid = bytes(info.hardware_version.unique_id)
        check("name/uid populated", len(name) > 0 and any(uid), "(%s, uid %s)" % (name, uid.hex()))

    if a.actuator:
        print("stage 4: kill watchdog (status code bit0 = killed)")
        killed = lambda: a.node in status and (status[a.node].vendor_specific_status_code & 1) == 1
        armed = lambda: a.node in status and (status[a.node].vendor_specific_status_code & 1) == 0
        check("boots killed", wait_for(killed, 1.5))
        pub = KillPublisher(node, armed=True)
        check("arms on fresh Kill(armed)", wait_for(armed, 2.5))
        pub.enabled = False
        t0 = time.monotonic()
        ok = wait_for(killed, 2.5)
        check("kills when Kill stops", ok, "(seen after %.2fs; NodeStatus is 1 Hz)" % (time.monotonic() - t0))
        pub.enabled, pub.armed = True, True
        check("re-arms", wait_for(armed, 2.5))
        pub.armed = False
        check("kills on Kill(killed)", wait_for(killed, 2.5))
        pub.enabled = False

    if a.thruster:
        print("stage 5: thruster (ramp, kill, re-arm, stale) on channel 1 of node %d" % a.node)
        slot = THRUSTER_SLICE[a.node]
        seen = []                                   # (time, applied[0], killed, stale)
        host.add_handler(ThrusterStatus, lambda e: seen.append(
            (time.monotonic(), e.message.applied[0], bool(e.message.killed), bool(e.message.stale)))
            if e.transfer.source_node_id == a.node else None)
        values = [0] * 6
        cmd = CommandPublisher(host, values)
        kill = KillPublisher(node, armed=False)
        last = lambda: seen[-1] if seen else (0, None, None, None)

        t0 = time.monotonic(); wait_for(lambda: False, 1.0)
        n = sum(1 for s_ in seen if s_[0] >= t0)
        check("ThrusterStatus at ~20 Hz", 15 <= n <= 25, "(%d in 1 s)" % n)

        def ramp_check(label):
            start = len(seen)
            ok = wait_for(lambda: last()[1] == 255, 2.0)
            mids = [s_[1] for s_ in seen[start:] if 0 < s_[1] < 255]
            check(label, ok and len(mids) >= 3,
                  "(%d intermediate values, first %s)" % (len(mids), mids[:1]))

        cmd.values[slot] = 255
        kill.armed = True
        ramp_check("ramps 0 -> 255 (not a step)")
        kill.armed = False
        t0 = time.monotonic()
        ok = wait_for(lambda: last()[2] is True and last()[1] == 0, 1.0)
        check("Kill(killed): killed flag, applied 0", ok, "(%.2fs, status is 20 Hz)" % (time.monotonic() - t0))
        kill.armed = True
        ramp_check("re-arm ramps from 0 again (Orion stepped back)")
        cmd.enabled = False
        ok_flag = wait_for(lambda: last()[3] is True, 1.0)
        ok_zero = wait_for(lambda: last()[1] == 0, 2.0)
        check("stale command: flag set, ramps to 0, still armed", ok_flag and ok_zero and last()[2] is False)
        kill.enabled = False

    print("%d/%d passed" % (sum(results), len(results)))
    return 0 if all(results) else 1


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("monitor"); s.add_argument("iface")
    s = sub.add_parser("kill"); s.add_argument("iface")
    g = s.add_mutually_exclusive_group(required=True)
    g.add_argument("--armed", dest="armed", action="store_true")
    g.add_argument("--killed", dest="armed", action="store_false")
    s.add_argument("--seconds", type=float, default=0)
    s = sub.add_parser("host"); s.add_argument("iface")
    s.add_argument("--soft-kill", action="store_true")
    s.add_argument("--seconds", type=float, default=0)
    s = sub.add_parser("restart"); s.add_argument("iface"); s.add_argument("node", type=int)
    s = sub.add_parser("selftest"); s.add_argument("iface"); s.add_argument("node", type=int)
    s.add_argument("--actuator", action="store_true")
    s.add_argument("--thruster", action="store_true")
    s = sub.add_parser("thrust"); s.add_argument("iface")
    s.add_argument("values", type=int, nargs="+", metavar="V")
    s.add_argument("--seconds", type=float, default=0)
    a = p.parse_args()
    return {"monitor": cmd_monitor, "kill": cmd_kill, "host": cmd_host, "restart": cmd_restart,
            "selftest": cmd_selftest, "thrust": cmd_thrust}[a.cmd](a) or 0


if __name__ == "__main__":
    sys.exit(main())
