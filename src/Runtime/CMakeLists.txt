intrinsic_add_module_library(IntrinsicRuntime)
set(target_name IntrinsicRuntime)

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Runtime.Engine.cppm
        Runtime.GraphicsBackend.cppm
        Runtime.AssetPipeline.cppm
        Runtime.SceneManager.cppm
        Runtime.RenderOrchestrator.cppm
        Runtime.Selection.cppm
        Runtime.SelectionModule.cppm
        Runtime.SceneSerializer.cppm
        EditorUI/Runtime.EditorUI.cppm
        PRIVATE
        Runtime.Engine.cpp
        Runtime.GraphicsBackend.cpp
        Runtime.AssetPipeline.cpp
        Runtime.SceneManager.cpp
        Runtime.RenderOrchestrator.cpp
        Runtime.Selection.cpp
        Runtime.SelectionModule.cpp
        Runtime.SceneSerializer.cpp
        EditorUI/Runtime.EditorUI.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_include_directories(${target_name} SYSTEM PUBLIC
        ${tinygltf_SOURCE_DIR}
)

target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        IntrinsicGraphics
        IntrinsicGeometry
        IntrinsicInterface
        IntrinsicECS
        IntrinsicRHI
        PRIVATE
        glfw
        nlohmann_json::nlohmann_json
)

add_subdirectory(Geometry)
add_subdirectory(ECS)
add_subdirectory(Graphics)
add_subdirectory(Interface)
add_subdirectory(RHI)
