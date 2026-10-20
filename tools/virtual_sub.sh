#!/usr/bin/env bash
# virtual_sub.sh - run a whole virtual sub on the mcast:0 bus.
#
#   tools/virtual_sub.sh            # from the repo root, after cmake --build build
#   BUILD=other_build tools/virtual_sub.sh
#
# Starts the real controller app (node 1), both thruster apps (10, 11) and generic
# stand-ins for BMB (20), Power (21), Sensor (30) and Acoustics (31). Each node is
# its own process with its own sockets, exactly like separate boards on one bus.
#
# Then, in other terminals:
#   python tools/bench.py monitor mcast:0          # every node, live
#   python tools/bench.py thrust mcast:0 100 0 0 -50 0 0   # act as the Jetson (node 2)
#   kill -USR1 <controller pid>                    # press the kill/arm button
# The controller arms only while the Jetson's HostHeartbeat is fresh, so start
# `thrust` (or `host`) first, then press the button.
#
# Ctrl-C stops every node. Logs: $LOG/<node>.log (default /tmp/auvcan_sub).
set -u
BUILD=${BUILD:-build}
LOG=${LOG:-/tmp/auvcan_sub}
mkdir -p "$LOG"
PIDS=()

start() {                     # name, command...
    local name=$1; shift
    if [ ! -x "$1" ]; then echo "missing $1 (run: cmake --build $BUILD)"; exit 1; fi
    "$@" >"$LOG/$name.log" 2>&1 &
    PIDS+=($!)
    printf "  %-11s pid %-6s %s\n" "$name" "$!" "$LOG/$name.log"
}
stop() { kill "${PIDS[@]}" 2>/dev/null; wait 2>/dev/null; echo "stopped"; }
trap stop EXIT
trap 'exit 0' INT TERM

echo "virtual sub on mcast:0"
start controller "$BUILD/bench_controller_host_linux";  CTRL=$!
start thruster_a "$BUILD/thruster_a_linux"
start thruster_b "$BUILD/thruster_b_linux"
start bmb        "$BUILD/virtual_node" 20 edu.cornell.cuauv.bmb
start power      "$BUILD/virtual_node" 21 edu.cornell.cuauv.power
start sensor     "$BUILD/virtual_node" 30 edu.cornell.cuauv.sensor
start acoustics  "$BUILD/virtual_node" 31 edu.cornell.cuauv.acoustics
echo
echo "arm / kill:  kill -USR1 $CTRL      (needs a host: bench.py thrust or host)"
echo "following controller + thruster logs (Ctrl-C to stop everything)"
echo
tail -n +1 -f "$LOG/controller.log" "$LOG/thruster_a.log" "$LOG/thruster_b.log" &
PIDS+=($!)
wait "$!"          # interruptible: a signal runs the trap at once, unlike a foreground tail
