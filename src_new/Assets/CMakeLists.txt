set(target_name ExtrinsicAssets)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Asset.EventBus.cppm
        Asset.LoadPipeline.cppm
        Asset.PathIndex.cppm
        Asset.PayloadStore.cppm
        Asset.Registry.cppm
        Asset.Service.cppm
        PRIVATE
        Asset.EventBus.cpp
        Asset.LoadPipeline.cpp
        Asset.PathIndex.cpp
        Asset.PayloadStore.cpp
        Asset.Registry.cpp
        Asset.Service.cpp
)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicCore
)

target_include_directories(${target_name} PUBLIC .)