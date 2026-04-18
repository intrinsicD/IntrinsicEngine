set(target_name ExtrinsicECS)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        ECS.Scene.Registry.cppm
        ECS.Scene.Handle.cppm
)

target_include_directories(${target_name} PUBLIC .)
target_link_libraries(${target_name} PRIVATE EnTT::EnTT)

add_subdirectory(Components)
add_subdirectory(Systems)