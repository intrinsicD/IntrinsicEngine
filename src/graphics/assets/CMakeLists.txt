set(target_name ExtrinsicGraphicsAssets)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Graphics.GpuAssetCache.cppm
        PRIVATE
        Graphics.GpuAssetCache.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicCore
        ExtrinsicAssets
        ExtrinsicRHI
)
