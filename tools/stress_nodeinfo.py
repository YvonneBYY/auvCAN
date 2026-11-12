#!/usr/bin/env python3
"""
stress_nodeinfo.py - does a board's multi-frame reply survive a busy bus?

    python tools/stress_nodeinfo.py IFACE NODE [--count 300]

Acts as the Jetson (node 2): RawCommand at 100 Hz + HostHeartbeat at 10 Hz to load
the bus, while requesting GetNodeInfo (an ~11-frame response) from NODE over and over.
The board's own ThrusterStatus/NodeStatus frames compete with that response for its
3 TX buffers, which is when FDCAN queue mode could send same-ID frames out of order.

Pass = every request answered. With the old queue-mode TX, lost responses show up
as timeouts here. Don't run it alongside sub.py, bench.py thrust/host, or a real Jetson:
it is node 2.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench   # noqa: E402  (DSDL loading, make_node, CommandPublisher)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("iface")
    p.add_argument("node", type=int)
    p.add_argument("--count", type=int, default=300)
    a = p.parse_args()

    node = bench.make_node(a.iface, bench.HOST_ID)
    bench.CommandPublisher(node, [0] * 6)          # bus load; zero thrust, so nothing moves

    ok = timeouts = 0
    t0 = time.monotonic()
    for i in range(a.count):
        box = {}
        node.request(bench.GetNodeInfo.Request(), a.node, lambda e: box.setdefault("r", e), timeout=0.5)
        end = time.monotonic() + 0.7
        while "r" not in box and time.monotonic() < end:
            node.spin(0.005)
        if box.get("r") is not None:
            ok += 1
        else:
            timeouts += 1
        if (i + 1) % 50 == 0:
            print("%4d requests: %d answered, %d timed out" % (i + 1, ok, timeouts))
    rate = a.count / (time.monotonic() - t0)
    print("%s: %d/%d answered (%.0f requests/s)" % ("PASS" if timeouts == 0 else "FAIL", ok, a.count, rate))
    return 0 if timeouts == 0 else 1


if __name__ == "__main__":
    sys.exit(main())