# Fixture: ExtrinsicRHI links only ExtrinsicCore from promoted layers.
add_library(FixtureRHI INTERFACE)
target_link_libraries(FixtureRHI INTERFACE ExtrinsicCore)
