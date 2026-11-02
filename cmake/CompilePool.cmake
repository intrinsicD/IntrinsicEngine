# Ninja starts nproc+2 compiles at once. A heavy C++23 module TU under ASan peaks at about 3.4 GB (measured
# 2026-11-02), so a full rebuild outgrew 62 GB of RAM and hung the machine. Compiles share a Ninja pool sized from
# the host's RAM: 4 GiB per compile leaves room for the desktop and links when every compile is a heavy one.
# Script mode, for the test: cmake -DTOTAL_MIB=<n> -P cmake/CompilePool.cmake prints the depth.
set(INTRINSIC_COMPILE_JOB_MIB 4096)

function(intrinsic_compile_pool_depth total_mib out)
    math(EXPR depth "${total_mib} / ${INTRINSIC_COMPILE_JOB_MIB}")
    if(depth LESS 1)
        set(depth 1)
    endif()
    set(${out} ${depth} PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
    intrinsic_compile_pool_depth(${TOTAL_MIB} depth)
    message("${depth}")
    return()
endif()

# shortcut: the host's physical RAM, not a container's memory limit (cgroup); size from the limit before building
# in memory-limited containers.
cmake_host_system_information(RESULT _intrinsic_total_mib QUERY TOTAL_PHYSICAL_MEMORY)
intrinsic_compile_pool_depth(${_intrinsic_total_mib} _intrinsic_compile_depth)
set_property(GLOBAL APPEND PROPERTY JOB_POOLS intrinsic_compile=${_intrinsic_compile_depth})
set(CMAKE_JOB_POOL_COMPILE intrinsic_compile)
message(STATUS "Compile pool: ${_intrinsic_compile_depth} parallel compiles (${_intrinsic_total_mib} MiB RAM)")
