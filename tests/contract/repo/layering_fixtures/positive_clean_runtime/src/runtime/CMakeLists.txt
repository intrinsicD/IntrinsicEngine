# Fixture: runtime composes every lower layer at the CMake level.
add_library(FixtureRuntime INTERFACE)
target_link_libraries(FixtureRuntime
    INTERFACE
    ExtrinsicCore
    ExtrinsicECS
    ExtrinsicRHI
    ExtrinsicGraphics
    ExtrinsicAssets
    ExtrinsicPlatform
)
