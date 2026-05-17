# Fixture: graphics target links allowed lower-layer promoted targets.
add_library(FixtureGraphics INTERFACE)
target_link_libraries(FixtureGraphics
    INTERFACE
    ExtrinsicCore
    ExtrinsicRHI
    ExtrinsicAssets
    ExtrinsicGeometry
)
