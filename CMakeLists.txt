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
    ${GEN}/src/cuauv.HostHeartbeat.c
    ${GEN}/src/cuauv.ThrusterStatus.c
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
        add_executable(${target} boards/${appdir}/app.c boards/bench_common/bsp_linux.c
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

    # Thruster A / B as Linux processes on mcast:0 (same app.c + thruster.c as the G4 build).
    foreach(v a b)
        add_executable(thruster_${v}_linux boards/thruster/app.c boards/thruster/thruster.c
                       boards/thruster/pwm_linux.c boards/bench_common/bsp_linux.c
                       ports/linux_mcast/auvcan_port_mcast.c)
        target_include_directories(thruster_${v}_linux PRIVATE boards/thruster boards/bench_common
                                   ports/linux_mcast)
        target_link_libraries(thruster_${v}_linux PRIVATE auvcan_core)
        target_compile_options(thruster_${v}_linux PRIVATE -Wall -Wextra -Werror)
        if (AUVCAN_SANITIZE)
            target_compile_options(thruster_${v}_linux PRIVATE -fsanitize=address,undefined -g)
            target_link_options(thruster_${v}_linux PRIVATE -fsanitize=address,undefined)
        endif()
    endforeach()
    target_compile_definitions(thruster_b_linux PRIVATE THRUSTER_B)
    add_bench_app(bench_controller_linux  bench_controller)
    add_bench_app(bench_controller_host_linux bench_controller)
    target_compile_definitions(bench_controller_host_linux PRIVATE BOARD_REQUIRE_HOST=1)
endif()
