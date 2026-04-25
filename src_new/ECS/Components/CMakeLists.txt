target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        ECS.Component.AssetInstance.cppm
        ECS.Component.Collider.cppm
        ECS.Component.Culling.Local.cppm
        ECS.Component.Culling.World.cppm
        ECS.Component.Culling.Proxy.cppm
        ECS.Component.GeometrySources.cppm
        ECS.Component.Hierarchy.cppm
        ECS.Component.Light.cppm
        ECS.Component.MetaData.cppm
        ECS.Component.Selection.cppm
        ECS.Component.ShadowCaster.cppm
        ECS.Component.Transform.Local.cppm
        ECS.Component.Transform.World.cppm
        PRIVATE
        ECS.Component.Transform.Local.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name} PRIVATE
        ExtrinsicCore
        ExtrinsicAssets
        IntrinsicGeometry
        glm::glm)
