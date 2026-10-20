#!/bin/sh
# export_cubeide.sh - copy exactly the files one bench firmware needs into a
# CubeIDE project, as a single "auvcan" folder.
#
#   tools/export_cubeide.sh controller ~/STM32CubeIDE/workspace/auvcan_boss
#   tools/export_cubeide.sh node       ~/STM32CubeIDE/workspace/auvcan_node
#   tools/export_cubeide.sh thruster   ~/STM32CubeIDE/workspace/auvcan_thruster
#       (Thruster B: also add the symbol THRUSTER_B in the CubeIDE project)
#
# Creates <project>/auvcan/{core,port,libcanard,dsdl/include,dsdl/src,bsp,app}.
# Re-running replaces <project>/auvcan, so don't edit files in there; edit the
# repo and export again.
set -eu

ROLE=${1:-}; PROJ=${2:-}
case "$ROLE" in controller|node|thruster) ;; *)
    echo "usage: $0 controller|node|thruster <cubeide-project-dir>" >&2; exit 2 ;; esac
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
         cuauv.Kill cuauv.HostHeartbeat \
         cuauv.ThrusterStatus uavcan.equipment.esc.RawCommand; do
    cp "$REPO/generated/src/$t.c"                             "$OUT/dsdl/src/"
done
cp "$REPO"/boards/bench_common/bsp.h \
   "$REPO"/boards/bench_common/app.h \
   "$REPO"/boards/bench_common/bsp_g4.c \
   "$REPO"/boards/bench_common/hw_*.h                        "$OUT/bsp/"
if [ "$ROLE" = thruster ]; then
    cp "$REPO"/boards/thruster/app.c "$REPO"/boards/thruster/board_config.h \
       "$REPO"/boards/thruster/thruster.[ch] "$REPO"/boards/thruster/pwm.h \
       "$REPO"/boards/thruster/pwm_g4.c                       "$OUT/app/"
    SRC=thruster
else
    cp "$REPO/boards/bench_$ROLE/app.c" \
       "$REPO/boards/bench_$ROLE/board_config.h"             "$OUT/app/"
    SRC=bench_$ROLE
fi

echo "exported $SRC -> $OUT"
echo "CubeIDE include paths (Project > Properties > C/C++ Build > Settings > MCU GCC Compiler > Include paths):"
for d in core port libcanard dsdl/include bsp app; do echo "  ../auvcan/$d"; done
