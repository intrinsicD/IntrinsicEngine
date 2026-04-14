target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        ECS.Component.CpuGeometry.cppm
        ECS.Component.Hierarchy.cppm
        ECS.Component.MetaData.cppm
        ECS.Component.RenderGeometry.cppm
        ECS.Component.Transform.cppm
)

target_include_directories(${target_name} PUBLIC .)