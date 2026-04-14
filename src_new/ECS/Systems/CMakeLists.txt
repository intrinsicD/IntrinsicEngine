target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        ECS.System.RenderSync.cppm
        ECS.System.TransformHierarchy.cppm
        PRIVATE
        ECS.System.RenderSync.cpp
        ECS.System.TransformHierarchy.cpp
)

target_include_directories(${target_name} PUBLIC .)