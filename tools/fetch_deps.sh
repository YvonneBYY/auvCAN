#!/usr/bin/env bash
# Fetch the DSDL code-generator dependencies at the exact commits auvCAN was tested against.
# Only needed to REGENERATE generated/ after editing dsdl/. libcanard is vendored in
# third_party/libcanard/ (601ed35) and is not touched by this script.
# ST HAL/CMSIS are only needed for the standalone ARM test build; board projects
# get them from CubeMX.
set -euo pipefail
cd "$(dirname "$0")/../third_party"

fetch() {  # dir url commit
    if [ ! -d "$1/.git" ]; then git clone -q "$2" "$1"; fi
    git -C "$1" fetch -q origin "$3" 2>/dev/null || git -C "$1" fetch -q origin
    git -C "$1" checkout -q "$3"
    echo "$1 @ ${3:0:10}"
}

fetch dronecan_dsdl   https://github.com/dronecan/DSDL.git           b4653c7abc3c47cb31b16efa24ea755232774756
fetch dronecan_dsdlc  https://github.com/dronecan/dronecan_dsdlc.git 431170fa4bfe2212b516b8f33bdc796267907f1c

if [ "${1:-}" = "--st" ]; then
    mkdir -p st
    fetch st/stm32g4xx_hal_driver https://github.com/STMicroelectronics/stm32g4xx_hal_driver.git a6001282dfacfff57e9710250f15e4333b578865
    fetch st/cmsis_device_g4      https://github.com/STMicroelectronics/cmsis_device_g4.git      626ee412334a5ed2e5b320af5a8d77d69f03a558
    fetch st/cmsis_core           https://github.com/STMicroelectronics/cmsis_core.git           afc5ca6af0a49232fde7eb4548dd0962d119ce14
fi
