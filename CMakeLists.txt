# Host build: core + simulated bus + tests.
# Firmware builds add core/, ports/stm32g4/, libcanard and the generated DSDL
# sources to the board's CubeMX/CubeIDE project instead (see README).
cmake_minimum_required(VERSION 3.16)
project(auvcan C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LIBCANARD ${CMAKE_SOURCE_DIR}/third_party/libcanard)
set(GEN       ${CMAKE_SOURCE_DIR}/generated)

# Only the DSDL types the core actually uses.
set(DSDL_SOURCES
    ${GEN}/src/uavcan.protocol.NodeStatus.c
    ${GEN}/src/uavcan.protocol.GetNodeInfo_req.c
    ${GEN}/src/uavcan.protocol.GetNodeInfo_res.c
    ${GEN}/src/uavcan.protocol.RestartNode_req.c
    ${GEN}/src/uavcan.protocol.RestartNode_res.c
    ${GEN}/src/cuauv.Kill.c
    ${GEN}/src/cuauv.HostHeartbeat.c
)

add_library(canard_dsdl STATIC ${LIBCANARD}/canard.c ${DSDL_SOURCES})
target_include_directories(canard_dsdl PUBLIC ${LIBCANARD} ${GEN}/include)

add_library(auvcan_core STATIC
    core/auvcan.c
    core/auvcan_fresh.c
    core/can_ring.c
)
target_include_directories(auvcan_core PUBLIC core)
target_link_libraries(auvcan_core PUBLIC canard_dsdl)
target_compile_options(auvcan_core PRIVATE -Wall -Wextra -Wshadow -Wconversion -Werror)

add_library(auvcan_sim STATIC ports/sim/auvcan_port_sim.c)
target_include_directories(auvcan_sim PUBLIC ports/sim)
target_link_libraries(auvcan_sim PUBLIC auvcan_core)
target_compile_options(auvcan_sim PRIVATE -Wall -Wextra -Werror)

add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE auvcan_sim)
target_compile_options(test_core PRIVATE -Wall -Wextra)

option(AUVCAN_SANITIZE "Build with ASan/UBSan" ON)
if (AUVCAN_SANITIZE)
    foreach(t canard_dsdl auvcan_core auvcan_sim test_core)
        target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(${t} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

enable_testing()
add_test(NAME core COMMAND test_core)

# Virtual board on pydronecan's UDP-multicast bus (Linux only).
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
    add_bench_app(bench_controller_linux  bench_controller)
    add_bench_app(bench_controller_host_linux bench_controller)
    target_compile_definitions(bench_controller_host_linux PRIVATE BOARD_REQUIRE_HOST=1)
endif()
