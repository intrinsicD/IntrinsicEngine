# Negative fixture: graphics_rhi must not link the promoted platform target.
add_library(FixtureBadRHI INTERFACE)
target_link_libraries(FixtureBadRHI INTERFACE ExtrinsicPlatform)
