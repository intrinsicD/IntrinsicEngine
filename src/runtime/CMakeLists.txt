set(target_name ExtrinsicRuntime)
intrinsic_add_module_library(${target_name})

option(INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN
        "Allow runtime to instantiate the promoted fail-closed Vulkan IDevice when RenderConfig opts in"
        OFF)

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Runtime.AssetGeometryIO.cppm
        Runtime.EcsSystemBundle.cppm
        Runtime.Engine.cppm
        Cameras/Runtime.CameraControllers.cppm
        Editor/Runtime.SandboxEditorUi.cppm
        ImGui/Runtime.ImGuiAdapter.cppm
        Runtime.GraphGeometryPacker.cppm
        Runtime.MeshGeometryPacker.cppm
        Runtime.MeshPrimitiveViewPacker.cppm
        Runtime.PointCloudGeometryPacker.cppm
        Runtime.PrimitiveSelectionRefinement.cppm
        Runtime.ProceduralGeometry.cppm
        Runtime.ProceduralGeometryPacker.cppm
        Runtime.ReferenceScene.cppm
        Runtime.RenderExtraction.cppm
        Runtime.SelectionController.cppm
        Runtime.StableEntityLookup.cppm
        Runtime.StreamingExecutor.cppm
        SpatialDebug/Runtime.SpatialDebugAdapters.cppm
        Visualization/Runtime.VisualizationAdapters.cppm
        PRIVATE
        Runtime.AssetGeometryIO.cpp
        Runtime.EcsSystemBundle.cpp
        Runtime.Engine.cpp
        Editor/Runtime.SandboxEditorUi.cpp
        ImGui/Runtime.ImGuiAdapter.cpp
        Runtime.GraphGeometryPacker.cpp
        Runtime.MeshGeometryPacker.cpp
        Runtime.MeshPrimitiveViewPacker.cpp
        Runtime.PointCloudGeometryPacker.cpp
        Runtime.PrimitiveSelectionRefinement.cpp
        Runtime.ProceduralGeometry.cpp
        Runtime.ProceduralGeometryPacker.cpp
        Runtime.ReferenceScene.cpp
        Runtime.RenderExtraction.cpp
        Runtime.SelectionController.cpp
        Runtime.StableEntityLookup.cpp
        Runtime.StreamingExecutor.cpp
        SpatialDebug/Runtime.SpatialDebugAdapters.cpp
        Visualization/Runtime.VisualizationAdapters.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicAssets
        ExtrinsicCore
        ExtrinsicECS
        ExtrinsicGraphics
        ExtrinsicGraphicsAssets
        ExtrinsicPlatform
        IntrinsicGeometry
        EnTT::EnTT
        glm::glm
)

# RUNTIME-090: the runtime-side Dear ImGui adapter links the backend-agnostic
# ImGui core PRIVATE so `imgui.h` (and its transitive deps) stay out of the
# runtime's public module surface. `imgui_core_lib` is always available, so the
# adapter builds in headless-no-glfw configs too.
target_link_libraries(${target_name} PRIVATE imgui_core_lib)

if(INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN)
        if(TARGET ExtrinsicBackendsVulkan)
                target_link_libraries(${target_name} PRIVATE ExtrinsicBackendsVulkan)
                target_compile_definitions(${target_name} PRIVATE EXTRINSIC_RUNTIME_HAS_PROMOTED_VULKAN=1)
        else()
                message(WARNING
                        "INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN=ON but ExtrinsicBackendsVulkan is not built; "
                        "runtime will keep using the Null fallback.")
        endif()
endif()
