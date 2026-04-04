intrinsic_add_module_library(IntrinsicRuntime)
set(target_name IntrinsicRuntime)

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Runtime.Engine.cppm
        Runtime.GraphicsBackend.cppm
        Runtime.AssetPipeline.cppm
        Runtime.AssetIngestService.cppm
        Runtime.SceneManager.cppm
        Runtime.RenderExtraction.cppm
        Runtime.RenderOrchestrator.cppm
        Runtime.ResourceMaintenance.cppm
        Runtime.Selection.cppm
        Runtime.SelectionModule.cppm
        Runtime.FrameLoop.cppm
        Runtime.SystemBundles.cppm
        Runtime.SceneSerializer.cppm
        Runtime.PointCloudKMeans.cppm
        EditorUI/Runtime.EditorUI.cppm
        PRIVATE
        Runtime.Engine.cpp
        Runtime.GraphicsBackend.cpp
        Runtime.AssetPipeline.cpp
        Runtime.AssetIngestService.cpp
        Runtime.SceneManager.cpp
        Runtime.RenderExtraction.cpp
        Runtime.RenderOrchestrator.cpp
        Runtime.ResourceMaintenance.cpp
        Runtime.Selection.cpp
        Runtime.SelectionModule.cpp
        Runtime.FrameLoop.cpp
        Runtime.SystemBundles.cpp
        Runtime.SceneSerializer.cpp
        Runtime.PointCloudKMeans.cpp
        EditorUI/Runtime.EditorUI.cpp
        EditorUI/Runtime.EditorUI.Widgets.cpp
        EditorUI/Runtime.EditorUI.InspectorController.cpp
        EditorUI/Runtime.EditorUI.EntityCommands.cpp
        EditorUI/Runtime.EditorUI.GeometryWorkflowController.cpp
        EditorUI/Runtime.EditorUI.SpatialDebugController.cpp
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

if(INTRINSIC_ENABLE_CUDA)
    add_library(IntrinsicRuntimePointCloudKMeansCuda OBJECT Runtime.PointCloudKMeans.cu)
    set_target_properties(IntrinsicRuntimePointCloudKMeansCuda PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            CUDA_STANDARD 17
            CUDA_STANDARD_REQUIRED ON)
    set_target_properties(${target_name} PROPERTIES
            CUDA_STANDARD 17
            CUDA_STANDARD_REQUIRED ON)
    target_link_libraries(IntrinsicRuntimePointCloudKMeansCuda PRIVATE IntrinsicConfig CUDA::cudart CUDA::cuda_driver)
    target_include_directories(IntrinsicRuntimePointCloudKMeansCuda PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_sources(${target_name} PRIVATE $<TARGET_OBJECTS:IntrinsicRuntimePointCloudKMeansCuda>)
    target_link_libraries(${target_name} PRIVATE CUDA::cudart)
endif()

add_subdirectory(Geometry)
add_subdirectory(ECS)
add_subdirectory(Graphics)
add_subdirectory(Interface)
add_subdirectory(RHI)
