set(target_name IntrinsicECS)
add_library(${target_name})

set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_options(${target_name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_compile_features(${target_name} PUBLIC cxx_std_23)
target_link_options(${target_name} PRIVATE ${INTRINSIC_LINK_FLAGS})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        ECS.cppm
        Components/ECS.Components.cppm
        Components/ECS.Components.AxisRotator.cppm
        Components/ECS.Components.Hierarchy.cppm
        Components/ECS.Components.NameTag.cppm
        Components/ECS.Components.Transform.cppm
        Components/ECS.Components.Selection.cppm
        ECS.Systems.Transform.cppm
        ECS.Scene.cppm
        FILE_SET ecs_impl TYPE CXX_MODULES FILES
        Components/ECS.Components.Hierarchy.cpp
        ECS.Systems.Transform.cpp
        ECS.Scene.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        EnTT::EnTT
)
