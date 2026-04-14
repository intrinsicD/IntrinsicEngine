add_executable(ExtrinsicSandbox)

target_sources(ExtrinsicSandbox
        PRIVATE
        Sandbox.cppm
        main.cpp
)

target_link_libraries(ExtrinsicSandbox
        PRIVATE
        ExtrinsicRuntime
)