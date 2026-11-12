#!/bin/sh
# export_cubeide.sh - copy exactly the files one bench firmware needs into a
# CubeIDE project, as a single "auvcan" folder.
#
#   tools/export_cubeide.sh controller ~/STM32CubeIDE/workspace/auvcan_boss
#   tools/export_cubeide.sh node       ~/STM32CubeIDE/workspace/auvcan_node
#   tools/export_cubeide.sh sensor     ~/STM32CubeIDE/workspace/auvcan_sensor   (sensors + 8 thrusters)
#   tools/export_cubeide.sh slcan_bridge ~/STM32CubeIDE/workspace/auvcan_bridge
#
# Creates <project>/auvcan/{core,port,libcanard,dsdl/include,dsdl/src,bsp,app}.
# Re-running replaces <project>/auvcan, so don't edit files in there; edit the
# repo and export again.
set -eu

ROLE=${1:-}; PROJ=${2:-}
case "$ROLE" in controller|node|sensor|slcan_bridge) ;;
    thruster) echo "the thruster board is retired: the sensor board drives the thrusters (use: $0 sensor ...)" >&2; exit 2 ;;
    *) echo "usage: $0 controller|node|sensor|slcan_bridge <cubeide-project-dir>" >&2; exit 2 ;; esac
[ -d "$PROJ" ] || { echo "no such project dir: $PROJ" >&2; exit 2; }

REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT="$PROJ/auvcan"
rm -rf "$OUT"
mkdir -p "$OUT/core" "$OUT/port" "$OUT/libcanard" "$OUT/dsdl/include" "$OUT/dsdl/src" "$OUT/bsp" "$OUT/app"

cp "$REPO"/core/*.c "$REPO"/core/*.h                         "$OUT/core/"
cp "$REPO"/ports/stm32g4/auvcan_port_g4.[ch]                  "$OUT/port/"
cp "$REPO"/third_party/libcanard/canard.c \
   "$REPO"/third_party/libcanard/canard.h \
   "$REPO"/third_party/libcanard/canard_internals.h \
   "$REPO"/third_party/libcanard/LICENSE                     "$OUT/libcanard/"
cp -R "$REPO"/generated/include/.                             "$OUT/dsdl/include/"
for t in uavcan.protocol.NodeStatus \
         uavcan.protocol.GetNodeInfo_req uavcan.protocol.GetNodeInfo_res \
         uavcan.protocol.RestartNode_req uavcan.protocol.RestartNode_res \
         cuauv.Kill cuauv.KillRequest cuauv.HostHeartbeat \
         cuauv.ThrusterStatus uavcan.equipment.esc.RawCommand \
         cuauv.Depth cuauv.HullStatus; do
    cp "$REPO/generated/src/$t.c"                             "$OUT/dsdl/src/"
done
cp "$REPO"/boards/bench_common/bsp.h \
   "$REPO"/boards/bench_common/app.h \
   "$REPO"/boards/bench_common/bsp_g4.c \
   "$REPO"/boards/bench_common/statuslog.[ch] \
   "$REPO"/boards/bench_common/log_usbcdc_g4.c \
   "$REPO"/boards/bench_common/log_uart_g4.c \
   "$REPO"/boards/bench_common/hw_*.h                        "$OUT/bsp/"
if [ "$ROLE" = sensor ]; then
    cp "$REPO"/boards/sensor/app.c "$REPO"/boards/sensor/board_config.h \
       "$REPO"/boards/sensor/sensor.[ch] "$REPO"/boards/sensor/sensor_hw.h \
       "$REPO"/boards/sensor/hw_g4.c \
       "$REPO"/boards/thruster/thruster.[ch] "$REPO"/boards/thruster/pwm.h \
       "$REPO"/boards/thruster/pwm_g4.c                       "$OUT/app/"
    SRC=sensor
elif [ "$ROLE" = slcan_bridge ]; then
    cp "$REPO"/boards/slcan_bridge/app.c "$REPO"/boards/slcan_bridge/board_config.h \
       "$REPO"/boards/slcan_bridge/slcan.[ch] "$REPO"/boards/slcan_bridge/stream.h \
       "$REPO"/boards/slcan_bridge/stream_g4.c \
       "$REPO"/boards/slcan_bridge/stream_usbcdc_g4.c         "$OUT/app/"
    SRC=slcan_bridge
else
    cp "$REPO/boards/bench_$ROLE/app.c" \
       "$REPO/boards/bench_$ROLE/board_config.h"             "$OUT/app/"
    SRC=bench_$ROLE
fi

echo "exported $SRC -> $OUT"
echo "CubeIDE include paths (Project > Properties > C/C++ Build > Settings > MCU GCC Compiler > Include paths):"
for d in core port libcanard dsdl/include bsp app; do echo "  ../auvcan/$d"; done
