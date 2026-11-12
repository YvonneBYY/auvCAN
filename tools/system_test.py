#!/usr/bin/env python3
"""
system_test.py - the whole sub on a virtual CAN bus, driven through real scenarios.

    python tools/system_test.py              # after cmake --build build
    python tools/system_test.py --bus 9 --keep-logs

Starts, on its own virtual bus (mcast:N, default 9, so it never disturbs a virtual
sub on mcast:0):
    node 1   power board kill logic, real mode (needs the host's heartbeat)
    node 2   the host (host/auvcan_host.py), controlled over its JSON/UDP port
    node 30  sensor board: simulated chips (fixed depth) + the 8 thruster outputs
    node 20, 31  battery / acoustics stand-ins (NodeStatus only)
and itself joins as node 3 (the debug laptop) to watch and to send operator kills.
A board that answers RestartNode exits with 42 and is started again, like a reboot.

Each scenario prints PASS / FAIL with what was measured. Exit code 0 = all passed.
"""
import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
import bench                                    # noqa: E402  (DSDL types, make_node)

Kill, ThrusterStatus, Depth, HullStatus = bench.Kill, bench.ThrusterStatus, bench.Depth, bench.HullStatus
NodeStatus, KillRequest = bench.NodeStatus, bench.KillRequest
THRUST = [100, 0, 0, 0, 0, 0, 0, -50]
FIXED_DEPTH = 1.5


class Proc:
    def __init__(self, name, argv, env, log_dir):
        self.name, self.argv, self.env, self.log_dir = name, argv, env, log_dir
        self.p, self.restarts = None, 0
        self.start()

    def start(self):
        log = open(os.path.join(self.log_dir, self.name + ".log"), "a")
        self.p = subprocess.Popen(self.argv, env=self.env, stdout=log, stderr=subprocess.STDOUT)

    def check_reboot(self):
        if self.p.poll() == 42:                 # RestartNode: come back like a rebooted board
            self.restarts += 1
            self.start()

    def signal(self, sig):
        self.p.send_signal(sig)

    def stop(self):
        if self.p.poll() is None:
            self.p.send_signal(signal.SIGCONT)
            self.p.terminate()
            try:
                self.p.wait(2)
            except subprocess.TimeoutExpired:
                self.p.kill()


class Harness:
    def __init__(self, a):
        self.bus = a.bus
        self.log_dir = tempfile.mkdtemp(prefix="auvcan_system_")
        self.leak_file = os.path.join(self.log_dir, "leak")
        open(self.leak_file, "w").write("0000")
        build = os.path.abspath(a.build)
        env = dict(os.environ, AUVCAN_BUS=str(a.bus), AUVCAN_LEAK_FILE=self.leak_file,
                   AUVCAN_SENSOR_DEPTH=str(FIXED_DEPTH), AUVCAN_STATUS="0")
        env.pop("AUVCAN_STATUS")
        self.ctl_port = 47900 + a.bus
        exe = lambda n: os.path.join(build, n)
        for n in ("bench_controller_host_linux", "sensor_linux", "virtual_node"):
            if not os.path.exists(exe(n)):
                sys.exit("missing %s - run: cmake --build %s" % (exe(n), a.build))
        self.procs = {
            "power":     Proc("power", [exe("bench_controller_host_linux")], env, self.log_dir),
            "sensor":    Proc("sensor", [exe("sensor_linux")], env, self.log_dir),
            "bmb":       Proc("bmb", [exe("virtual_node"), "20", "edu.cornell.cuauv.bmb"], env, self.log_dir),
            "acoustics": Proc("acoustics", [exe("virtual_node"), "31", "edu.cornell.cuauv.acoustics"], env, self.log_dir),
            "host":      Proc("host", [sys.executable, os.path.join(REPO, "host", "auvcan_host.py"),
                                       "--iface", "mcast:%d" % a.bus, "--control-port", str(self.ctl_port)],
                              env, self.log_dir),
        }
        self.node = bench.make_node("mcast:%d" % a.bus, bench.DEBUG_ID)
        self.kills, self.status, self.depths, self.hulls, self.ns = [], [], [], [], {}
        self.node.add_handler(Kill, lambda e: self.kills.append((time.monotonic(), e.message))
                              if e.transfer.source_node_id == 1 else None)
        self.node.add_handler(ThrusterStatus, lambda e: self.status.append((time.monotonic(), e.message)))
        self.node.add_handler(Depth, lambda e: self.depths.append((time.monotonic(), e.message)))
        self.node.add_handler(HullStatus, lambda e: self.hulls.append(
            (time.monotonic(), e.message, e.transfer.transfer_priority)))
        self.node.add_handler(NodeStatus, lambda e: self.ns.__setitem__(
            e.transfer.source_node_id, (time.monotonic(), e.message)))
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(1.0)
        self.feeding, self.next_feed, self.kr_seq = False, 0.0, 0
        self.results = []

    # ---- plumbing --------------------------------------------------------------
    def host(self, req):
        self.sock.sendto(json.dumps(req).encode(), ("127.0.0.1", self.ctl_port))
        return json.loads(self.sock.recv(1 << 20).decode())

    def spin(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.node.spin(0.005)
            for p in self.procs.values():
                p.check_reboot()
            if self.feeding and time.monotonic() >= self.next_feed:
                self.next_feed = time.monotonic() + 0.05            # the "autonomy" at 20 Hz
                try:
                    self.host({"cmd": "thrust", "values": THRUST})
                except (socket.timeout, OSError):
                    pass                                            # host stopped on purpose

    def wait(self, pred, timeout):
        """Spin until pred() is true; returns the elapsed seconds, or None on timeout."""
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout:
            if pred():
                return time.monotonic() - t0
            self.spin(0.005)
        return None

    def kill_msg(self):
        return self.kills[-1][1] if self.kills and time.monotonic() - self.kills[-1][0] < 0.3 else None

    def armed(self):
        k = self.kill_msg()
        return k is not None and bool(k.armed)

    def applied(self):
        return list(self.status[-1][1].applied) if self.status else None

    def thr_killed(self):
        return bool(self.status[-1][1].killed) if self.status else None

    def op_kill(self, active):
        self.node.broadcast(KillRequest(active=active, reason=bench.REASON_OPERATOR, seq=self.kr_seq & 0xFF),
                            priority=1)
        self.kr_seq += 1

    def press_switch(self):
        self.procs["power"].signal(signal.SIGUSR1)

    def check(self, name, ok, detail=""):
        self.results.append((name, ok, detail))
        print("  [%s] %s %s" % ("PASS" if ok else "FAIL", name, detail), flush=True)
        return ok

    def ms(self, t):
        return "never" if t is None else "%.0f ms" % (t * 1000)

    def stop(self):
        for p in self.procs.values():
            p.stop()


def run(h):
    print("bus mcast:%d, logs in %s" % (h.bus, h.log_dir))

    print("\n1. boot")
    t = h.wait(lambda: all(n in h.ns for n in (1, 2, 20, 30, 31)), 5.0)
    h.check("all five nodes online (1, 2, 20, 30, 31)", t is not None, h.ms(t))
    h.check("no node 10 (thruster board retired)", 10 not in h.ns)
    h.spin(0.5)
    k = h.kill_msg()
    h.check("boots KILLED by the hard switch, host seen", k is not None and not k.armed and k.hard_kill and k.host_ok)
    h.check("thruster reports killed, all outputs 0", h.thr_killed() is True and h.applied() == [0] * 8)

    print("\n2. arm and thrust")
    h.press_switch()
    t = h.wait(h.armed, 1.0)
    h.check("switch -> Kill(armed)", t is not None, h.ms(t))
    h.feeding = True
    t = h.wait(lambda: h.applied() == THRUST, 2.0)
    h.check("host thrust %s reaches all 8 outputs" % THRUST, t is not None, "(%s, includes the ramp)" % h.ms(t))

    print("\n3. control software stops sending")
    h.feeding = False
    t = h.wait(lambda: h.applied() == [0] * 8, 2.0)
    h.check("stale setpoint -> host sends zeros -> outputs back to 0", t is not None, h.ms(t))
    h.check("...and the sub stays armed", h.armed())

    print("\n4. soft kill from the host")
    h.feeding = True
    h.wait(lambda: h.applied() == THRUST, 2.0)
    h.host({"cmd": "soft_kill", "on": True})
    t = h.wait(lambda: h.kill_msg() is not None and h.kill_msg().soft_kill and not h.kill_msg().armed, 1.0)
    h.check("soft kill -> Kill(killed, soft)", t is not None, h.ms(t))
    t = h.wait(lambda: h.thr_killed() and h.applied() == [0] * 8, 1.0)
    h.check("thruster neutral at once", t is not None, h.ms(t))
    h.host({"cmd": "soft_kill", "on": False})
    t = h.wait(h.armed, 1.0)
    h.check("soft kill released -> armed again (switch still armed)", t is not None, h.ms(t))
    first = {}
    def ramping():
        a = h.applied()
        if a and a[0] not in (0, 100) and "mid" not in first:
            first["mid"] = a[0]
        return a == THRUST
    h.wait(ramping, 2.0)
    h.check("thrust ramps up again from 0 (no jump)", "mid" in first, "(saw T1 = %s on the way)" % first.get("mid"))

    print("\n5. operator kill from the debug laptop (node 3)")
    h.op_kill(True)
    t = h.wait(lambda: h.kill_msg() is not None and h.kill_msg().node_kill and h.kill_msg().node_kill_src == 3, 1.0)
    h.check("KillRequest(operator) -> Kill(node_kill, src 3)", t is not None, h.ms(t))
    h.spin(2.0)
    h.check("latched: still killed 2 s later with nobody sending", not h.armed() and h.thr_killed())
    h.op_kill(False)
    h.spin(0.3)
    h.check("clear refused while the switch is armed", not h.armed())
    h.press_switch()                                    # switch -> killed: clears the latch
    h.wait(lambda: h.kill_msg() is not None and not h.kill_msg().node_kill, 1.0)
    h.press_switch()                                    # switch -> armed
    t = h.wait(h.armed, 1.0)
    h.check("switch to killed and back -> armed", t is not None, h.ms(t))

    print("\n6. host hangs")
    h.feeding = False
    h.procs["host"].signal(signal.SIGSTOP)
    t0 = time.monotonic()
    t = h.wait(lambda: h.kill_msg() is not None and not h.kill_msg().host_ok and not h.kill_msg().armed, 2.0)
    h.check("no heartbeat -> Kill(killed, host_ok = 0)", t is not None, "%s (power board timeout 500 ms)" % h.ms(t))
    t = h.wait(lambda: h.thr_killed(), 1.0)
    h.check("thruster killed", t is not None, h.ms(None if t is None else time.monotonic() - t0))
    h.press_switch()                                    # a press while the host is gone does nothing
    h.spin(0.3)
    h.check("switch press while the host is gone is ignored", h.kill_msg() is not None and h.kill_msg().hard_kill)
    h.procs["host"].signal(signal.SIGCONT)
    h.wait(lambda: h.kill_msg() is not None and h.kill_msg().host_ok, 2.0)
    h.spin(1.0)
    k = h.kill_msg()
    h.check("host back -> stays KILLED (host loss = hard kill)", k is not None and not k.armed and k.hard_kill and k.host_ok)
    h.check("...thruster still killed", h.thr_killed() is True)
    h.press_switch()
    t = h.wait(h.armed, 1.0)
    h.check("switch press -> armed", t is not None, h.ms(t))

    print("\n7. power board hangs")
    h.procs["power"].signal(signal.SIGSTOP)
    t0 = time.monotonic()
    t = h.wait(lambda: h.thr_killed(), 2.0)
    h.check("no Kill messages -> thruster kills itself", t is not None, "%s (thruster timeout 250 ms)" % h.ms(t))
    h.procs["power"].signal(signal.SIGCONT)
    h.wait(h.armed, 2.0)

    print("\n8. sensors")
    n0 = len(h.depths)
    h.spin(1.0)
    rate = len(h.depths) - n0
    d = h.depths[-1][1] if h.depths else None
    h.check("Depth at ~50 Hz", 45 <= rate <= 55, "(%d in 1 s)" % rate)
    h.check("depth reads the simulated %.2f m" % FIXED_DEPTH, d is not None and d.sensor_ok and abs(d.depth_m - FIXED_DEPTH) < 0.01,
            "(%.3f m)" % (d.depth_m if d else float("nan")))
    code = lambda: h.ns[30][1].vendor_specific_status_code
    h.check("sensor board NodeStatus: bit 0 = 0 (armed), no leak bits", code() & 0x1F == 0, "(code 0x%02X)" % code())
    open(h.leak_file, "w").write("0010")
    t0 = time.monotonic()
    t = h.wait(lambda: any(x[1].leak[2] for x in h.hulls if x[0] >= t0), 1.0)
    ev = next((x for x in h.hulls if x[0] >= t0 and x[1].leak[2]), None)
    h.check("leak 3 -> HullStatus within 60 ms", t is not None and t < 0.06, h.ms(t))
    h.check("...sent at the hull-event priority (2)", ev is not None and ev[2] == 2, "(priority %s)" % (ev[2] if ev else None))
    h.spin(0.2)
    evs = h.host({"cmd": "state"})["state"]["events"]
    h.check("host logged the leak", any("LEAK 3" in e for e in evs))
    h.check("leak doesn't kill the sub (report only)", h.armed())
    t = h.wait(lambda: code() & 0x1E == 0x08, 1.5)
    h.check("NodeStatus: leak 3 = bit 3, killed bit still 0", t is not None and code() & 1 == 0, "(code 0x%02X)" % code())
    open(h.leak_file, "w").write("0000")

    print("\n8b. one board, two jobs")
    h.feeding = True
    h.wait(lambda: h.applied() == THRUST, 2.0)
    n0 = len(h.depths)
    h.spin(1.0)
    h.check("depth stays at 50 Hz while thrusting", 45 <= len(h.depths) - n0 <= 55, "(%d in 1 s)" % (len(h.depths) - n0))
    h.host({"cmd": "soft_kill", "on": True})
    h.wait(lambda: h.thr_killed(), 1.0)
    n0 = len(h.depths)
    h.spin(1.0)
    h.check("sensors keep publishing while killed", 45 <= len(h.depths) - n0 <= 55 and h.depths[-1][1].sensor_ok,
            "(%d in 1 s)" % (len(h.depths) - n0))
    t = h.wait(lambda: code() & 1 == 1, 1.5)
    h.check("NodeStatus: killed bit 0 set, sensor bits unaffected", t is not None and code() & 0xFE == 0, "(code 0x%02X)" % code())
    h.host({"cmd": "soft_kill", "on": False})
    h.feeding = False
    h.wait(h.armed, 1.0)

    print("\n9. RestartNode")
    up_before = h.ns[30][1].uptime_sec
    h.host({"cmd": "restart", "node": 30})
    t0 = time.monotonic()
    t = h.wait(lambda: h.procs["sensor"].restarts >= 1 and h.ns[30][1].uptime_sec < up_before, 4.0)
    h.check("host restarts the sensor board; it comes back", t is not None, h.ms(t))
    t0 = time.monotonic()                               # from here on: the rebooted board only
    t = h.wait(lambda: h.thr_killed() is False and h.status[-1][0] > t0, 2.0)
    h.check("rebooted board's thrusters re-arm from the live Kill stream", t is not None, h.ms(t))
    t = h.wait(lambda: h.depths and h.depths[-1][0] > t0 and h.depths[-1][1].sensor_ok, 2.0)
    h.check("...and depth is back", t is not None, h.ms(t))

    print("\n10. host state")
    st = h.host({"cmd": "state"})["state"]
    names = {k: v["name"] for k, v in st["nodes"].items()}
    h.check("host knows every board by name", all(names.get(str(n)) for n in (1, 20, 30, 31)), str(names))
    h.check("host has thruster, depth and hull telemetry",
            all(k in st["telemetry"] for k in ("thrusters/30", "depth/30", "hull/30")))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bus", type=int, default=9, help="virtual bus mcast:N (default 9)")
    p.add_argument("--build", default=os.path.join(REPO, "build"))
    p.add_argument("--keep-logs", action="store_true")
    a = p.parse_args()
    h = Harness(a)
    try:
        run(h)
    finally:
        h.stop()
    failed = [r for r in h.results if not r[1]]
    print("\n%d/%d passed%s" % (len(h.results) - len(failed), len(h.results),
                                "" if not failed else ", FAILED: " + "; ".join(r[0] for r in failed)))
    if failed or a.keep_logs:
        print("board logs: %s" % h.log_dir)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
