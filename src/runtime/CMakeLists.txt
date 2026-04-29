set(target_name ExtrinsicRuntime)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Runtime.Engine.cppm
        Runtime.FrameClock.cppm
        Runtime.StreamingExecutor.cppm
        PRIVATE
        Runtime.Engine.cpp
        Runtime.StreamingExecutor.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicAssets
        ExtrinsicCore
        ExtrinsicECS
        ExtrinsicGraphics
        ExtrinsicPlatform
)
