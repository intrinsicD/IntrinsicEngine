set(target_name ExtrinsicAssets)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Asset.Database.cppm
        Asset.Handle.cppm
        Asset.Manager.cppm
        PRIVATE
        Asset.Database.cpp
        Asset.Manager.cpp
)

target_include_directories(${target_name} PUBLIC .)