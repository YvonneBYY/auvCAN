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
  selftest IFACE NODE [--actuator]
                                automated stage-2/stage-4 bring-up checks against NODE

Only use `kill` / `selftest --actuator` on a bench WITHOUT a real controller board:
both act as node 1.

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

CONTROLLER_ID = 1
HOST_ID = 2
MONITOR_ID = 126
HOST_HB_PERIOD = 0.1
PRIO_HOST_HB = 6
KILL_PERIOD = 0.05
PRIO_KILL = 0
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
    node = make_node(a.iface, CONTROLLER_ID if a.actuator else MONITOR_ID)
    status = {}
    node.add_handler(NodeStatus, lambda e: status.__setitem__(e.transfer.source_node_id, e.message)
                     if e.transfer.source_node_id == a.node else None)
    results = []

    def check(name, ok, detail=""):
        results.append(ok)
        print("  [%s] %s %s" % ("PASS" if ok else "FAIL", name, detail))

    def wait_for(pred, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            node.spin(0.01)
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
    a = p.parse_args()
    return {"monitor": cmd_monitor, "kill": cmd_kill, "host": cmd_host, "restart": cmd_restart,
            "selftest": cmd_selftest}[a.cmd](a) or 0


if __name__ == "__main__":
    sys.exit(main())
