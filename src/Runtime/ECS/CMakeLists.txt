intrinsic_add_module_library(IntrinsicECS)
set(target_name IntrinsicECS)

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        ECS.cppm
        Components/ECS.Components.AxisRotator.cppm
        Components/ECS.Components.Hierarchy.cppm
        Components/ECS.Components.NameTag.cppm
        Components/ECS.Components.Transform.cppm
        Components/ECS.Components.Selection.cppm
        Components/ECS.Components.DEC.cppm
        Components/ECS.Components.cppm
        Systems/ECS.Systems.AxisRotator.cppm
        Systems/ECS.Systems.Transform.cppm
        ECS.Scene.cppm
        FILE_SET ecs_impl TYPE CXX_MODULES FILES
        Components/ECS.Components.Hierarchy.cpp
        Systems/ECS.Systems.AxisRotator.cpp
        Systems/ECS.Systems.Transform.cpp
        ECS.Scene.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        IntrinsicGeometry
        EnTT::EnTT
)
