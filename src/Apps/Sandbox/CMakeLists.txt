set(target_name Sandbox)
add_executable(${target_name} main.cpp)

# Standard strict flags
target_compile_options(${target_name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_compile_features(${target_name} PUBLIC cxx_std_23)
target_link_options(${target_name} PRIVATE ${INTRINSIC_LINK_FLAGS})
set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES ON)

target_link_libraries(${target_name}
        PRIVATE
        IntrinsicConfig
        IntrinsicCore
        IntrinsicRuntime
)

include(${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake)