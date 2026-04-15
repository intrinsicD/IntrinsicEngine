set(target_name ExtrinsicAssets)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Asset.Error.cppm
        Asset.Handle.cppm
        Asset.Manager.cppm
        PRIVATE
        Asset.Manager.cpp
)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicCore
        EnTT::EnTT
)

target_include_directories(${target_name} PUBLIC .)