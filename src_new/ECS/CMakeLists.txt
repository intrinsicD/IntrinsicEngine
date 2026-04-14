set(target_name ExtrinsicECS)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        ECS.Scene.Database.cppm
        PRIVATE
        ECS.Scene.Database.cpp
)

target_include_directories(${target_name} PUBLIC .)

add_subdirectory(Components)
add_subdirectory(Systems)