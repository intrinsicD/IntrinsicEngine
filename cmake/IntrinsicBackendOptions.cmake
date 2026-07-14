include_guard(GLOBAL)

# Shared build-graph inputs must exist before any layer subdirectory consumes
# them. Cache assignments without FORCE preserve explicit preset and -D values.
set(EXTRINSIC_PLATFORM "${CMAKE_SYSTEM_NAME}" CACHE STRING
    "Host platform selection")
set(EXTRINSIC_BACKEND "Vulkan" CACHE STRING
    "Graphics backend selection")
set_property(CACHE EXTRINSIC_BACKEND PROPERTY STRINGS Vulkan Null)
