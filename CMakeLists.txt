# Host build: core + simulated bus + tests.
# Firmware builds add core/, ports/stm32g4/, libcanard and the generated DSDL
# sources to the board's CubeMX/CubeIDE project instead (see README).
cmake_minimum_required(VERSION 3.16)
project(auvcan C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LIBCANARD ${CMAKE_SOURCE_DIR}/third_party/libcanard)   #vendored libcanard
set(GEN       ${CMAKE_SOURCE_DIR}/generated)               #generated DSDL code

# Only the DSDL types the core actually uses.
set(DSDL_SOURCES
    ${GEN}/src/uavcan.protocol.NodeStatus.c
    ${GEN}/src/uavcan.protocol.GetNodeInfo_req.c
    ${GEN}/src/uavcan.protocol.GetNodeInfo_res.c
    ${GEN}/src/uavcan.protocol.RestartNode_req.c
    ${GEN}/src/uavcan.protocol.RestartNode_res.c
    ${GEN}/src/cuauv.Kill.c
    ${GEN}/src/cuauv.KillRequest.c
    ${GEN}/src/cuauv.HostHeartbeat.c
    ${GEN}/src/cuauv.ThrusterStatus.c
    ${GEN}/src/cuauv.Depth.c
    ${GEN}/src/cuauv.HullStatus.c
    ${GEN}/src/uavcan.equipment.esc.RawCommand.c
)

add_library(canard_dsdl STATIC ${LIBCANARD}/canard.c ${DSDL_SOURCES})
target_include_directories(canard_dsdl PUBLIC ${LIBCANARD} ${GEN}/include)
# the include dir enables anything that links this directory automatically gets canard.h
# and the generated headers on its include path

add_library(auvcan_core STATIC
    core/auvcan.c
    core/auvcan_fresh.c
    core/can_ring.c
)
target_include_directories(auvcan_core PUBLIC core) #anything that links the core gets libcanard
target_link_libraries(auvcan_core PUBLIC canard_dsdl)
target_compile_options(auvcan_core PRIVATE -Wall -Wextra -Wshadow -Wconversion -Werror) 
# use strictest warnings for build

add_library(auvcan_sim STATIC ports/sim/auvcan_port_sim.c) #auvcan_sim -- virtual bus
target_include_directories(auvcan_sim PUBLIC ports/sim)
target_link_libraries(auvcan_sim PUBLIC auvcan_core)
target_compile_options(auvcan_sim PRIVATE -Wall -Wextra -Werror)

add_executable(test_core tests/test_core.c tests/dsdl_sizes.c)
target_link_libraries(test_core PRIVATE auvcan_sim)
target_compile_options(test_core PRIVATE -Wall -Wextra)

# adds AddressSanitizer and UndefinedBehaviorSanitizer to target
option(AUVCAN_SANITIZE "Build with ASan/UBSan" ON) 
if (AUVCAN_SANITIZE)
    foreach(t canard_dsdl auvcan_core auvcan_sim test_core)
        target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(${t} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

# Thruster board logic (boards/thruster/thruster.c) on the simulated bus.
add_library(thruster_logic STATIC boards/thruster/thruster.c)
target_include_directories(thruster_logic PUBLIC boards/thruster)
target_link_libraries(thruster_logic PUBLIC auvcan_core)
target_compile_options(thruster_logic PRIVATE -Wall -Wextra -Wshadow -Wconversion -Werror)

add_executable(test_thruster tests/test_thruster.c)
target_link_libraries(test_thruster PRIVATE thruster_logic auvcan_sim)
target_compile_options(test_thruster PRIVATE -Wall -Wextra)

if (AUVCAN_SANITIZE)
    foreach(t thruster_logic test_thruster)
        target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(${t} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

# Sensor board logic (boards/sensor/sensor.c) with simulated chips, on the simulated bus.
add_library(sensor_logic STATIC boards/sensor/sensor.c boards/sensor/sim_chips.c)
target_include_directories(sensor_logic PUBLIC boards/sensor)
target_link_libraries(sensor_logic PUBLIC auvcan_core m)
target_compile_options(sensor_logic PRIVATE -Wall -Wextra -Wshadow -Wconversion -Werror)
add_executable(test_sensor tests/test_sensor.c)
target_link_libraries(test_sensor PRIVATE sensor_logic auvcan_sim)
target_compile_options(test_sensor PRIVATE -Wall -Wextra)
add_test(NAME sensor COMMAND test_sensor)
if (AUVCAN_SANITIZE)
    foreach(t sensor_logic test_sensor)
        target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(${t} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

# SLCAN bridge codec (boards/slcan_bridge/slcan.c), no hardware.
add_library(slcan_codec STATIC boards/slcan_bridge/slcan.c)
target_include_directories(slcan_codec PUBLIC boards/slcan_bridge ${LIBCANARD})
target_compile_options(slcan_codec PRIVATE -Wall -Wextra -Wshadow -Wconversion -Werror)
add_executable(test_slcan tests/test_slcan.c)
target_link_libraries(test_slcan PRIVATE slcan_codec)
target_compile_options(test_slcan PRIVATE -Wall -Wextra)
add_test(NAME slcan COMMAND test_slcan)

# Bridge USB CDC link against a fake ST USB stack (boards/slcan_bridge/stream_usbcdc_g4.c).
add_executable(test_usbcdc tests/test_usbcdc.c boards/slcan_bridge/stream_usbcdc_g4.c)
target_include_directories(test_usbcdc PRIVATE tests/fake_usbd boards/slcan_bridge core)
target_compile_definitions(test_usbcdc PRIVATE BOARD_BRIDGE_USB=1 BOARD_NO_HW_PROFILE)
target_compile_options(test_usbcdc PRIVATE -Wall -Wextra -Werror)
add_test(NAME usbcdc COMMAND test_usbcdc)

enable_testing()
add_test(NAME core COMMAND test_core)
add_test(NAME thruster COMMAND test_thruster)

# Virtual board on pydronecan's UDP-multicast bus
if (UNIX)
    add_executable(virtual_node tools/virtual_node.c ports/linux_mcast/auvcan_port_mcast.c)
    target_include_directories(virtual_node PRIVATE ports/linux_mcast)
    target_link_libraries(virtual_node PRIVATE auvcan_core)
    target_compile_options(virtual_node PRIVATE -Wall -Wextra -Werror)
    if (AUVCAN_SANITIZE)
        target_compile_options(virtual_node PRIVATE -fsanitize=address,undefined -g)
        target_link_options(virtual_node PRIVATE -fsanitize=address,undefined)
    endif()
endif()

# Bench apps as Linux processes on the mcast:0 virtual bus (same app.c as the G4 build).
if (UNIX)
    function(add_bench_app target appdir)
        add_executable(${target} boards/${appdir}/app.c boards/bench_common/bsp_linux.c boards/bench_common/statuslog.c
                       ports/linux_mcast/auvcan_port_mcast.c)
        target_include_directories(${target} PRIVATE boards/${appdir} boards/bench_common ports/linux_mcast)
        target_link_libraries(${target} PRIVATE auvcan_core)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
        if (AUVCAN_SANITIZE)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -g)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endfunction()
    add_bench_app(bench_node_linux        bench_node)

    # The sensor board (node 30): simulated chips (boards/sensor/hw_linux.c) + the 8 thruster
    # outputs (boards/thruster/thruster.c, pwm_linux.c prints the pulses). Same app.c as the
    # G4 build. sensor_bench_linux obeys node 1's bench throttle instead of the host.
    foreach(t sensor_linux sensor_bench_linux)
        add_executable(${t} boards/sensor/app.c boards/sensor/hw_linux.c boards/thruster/pwm_linux.c
                       boards/bench_common/bsp_linux.c boards/bench_common/statuslog.c
                       ports/linux_mcast/auvcan_port_mcast.c)
        target_include_directories(${t} PRIVATE boards/sensor boards/thruster boards/bench_common ports/linux_mcast)
        target_link_libraries(${t} PRIVATE sensor_logic thruster_logic)
        target_compile_options(${t} PRIVATE -Wall -Wextra -Werror)
        if (AUVCAN_SANITIZE)
            target_compile_options(${t} PRIVATE -fsanitize=address,undefined -g)
            target_link_options(${t} PRIVATE -fsanitize=address,undefined)
        endif()
    endforeach()
    target_compile_definitions(sensor_bench_linux PRIVATE THRUSTER_CMD_SOURCE=1)
    add_bench_app(bench_controller_linux  bench_controller)
    add_bench_app(bench_controller_host_linux bench_controller)
    target_compile_definitions(bench_controller_host_linux PRIVATE BOARD_REQUIRE_HOST=1)
    # Bench throttle: boss publishes RawCommand from bench_cmd[]; thruster obeys node 1.
    add_bench_app(bench_controller_throttle_linux bench_controller)

    # SLCAN bridge on a PC: a pseudo-terminal as its UART, mcast:0 as its CAN bus.
    add_executable(slcan_bridge_linux boards/slcan_bridge/app.c boards/slcan_bridge/stream_linux.c
                   boards/bench_common/bsp_linux.c ports/linux_mcast/auvcan_port_mcast.c)
    target_include_directories(slcan_bridge_linux PRIVATE boards/slcan_bridge boards/bench_common ports/linux_mcast)
    target_link_libraries(slcan_bridge_linux PRIVATE slcan_codec auvcan_core)
    target_compile_options(slcan_bridge_linux PRIVATE -Wall -Wextra -Werror)

    if (AUVCAN_SANITIZE)
        foreach(t slcan_bridge_linux slcan_codec test_slcan)
            target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
            target_link_options(${t} PRIVATE -fsanitize=address,undefined)
        endforeach()
    endif()
    target_compile_definitions(bench_controller_throttle_linux PRIVATE BOARD_BENCH_THROTTLE=1)
endif()