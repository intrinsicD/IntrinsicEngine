set(target_name Sandbox)
add_executable(${target_name} main.cpp)

# Standard strict flags
target_compile_options(${target_name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_compile_features(${target_name} PUBLIC cxx_std_23)
target_link_options(${target_name} PRIVATE ${INTRINSIC_LINK_FLAGS})

target_link_libraries(${target_name}
        PRIVATE
        IntrinsicConfig
        IntrinsicRuntime
)

include(${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake)