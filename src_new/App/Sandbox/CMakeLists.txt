add_executable(ExtrinsicSandbox)
set_target_properties(ExtrinsicSandbox PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_options(ExtrinsicSandbox PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_compile_features(ExtrinsicSandbox PUBLIC cxx_std_23)
target_link_options(ExtrinsicSandbox PRIVATE ${INTRINSIC_LINK_FLAGS})

target_sources(ExtrinsicSandbox
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Sandbox.cppm
        PRIVATE
        main.cpp
)

target_link_libraries(ExtrinsicSandbox
        PRIVATE
        ExtrinsicRuntime
        ExtrinsicCore
)