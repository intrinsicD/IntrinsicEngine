set(target_name ExtrinsicRuntime)
intrinsic_add_module_library(${target_name})

option(INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN
        "Allow runtime to instantiate the promoted fail-closed Vulkan IDevice when RenderConfig opts in"
        ON)

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Runtime.AsyncWorkModule.cppm
        Runtime.AsyncBufferReadback.cppm
        Runtime.AssetWorkflowModule.cppm
        Runtime.AssetImportPipeline.cppm
        Runtime.AssetGeometryIO.cppm
        Runtime.AssetIngestStateMachine.cppm
        Runtime.AssetMeshNormals.cppm
        Runtime.AssetModelSceneHandoff.cppm
        Runtime.AssetModelTextureHandoff.cppm
        Runtime.AssetModelTextureIO.cppm
        Runtime.CommandBus.cppm
        Runtime.DerivedJobGraph.cppm
        Runtime.DeviceBootstrap.cppm
        Runtime.JobService.cppm
        Runtime.JobServiceGpuQueueBridge.cppm
        Modules/Clustering/Runtime.ClusteringModule.cppm
        Runtime.MeshAttributeTextureBake.cppm
        Runtime.MethodFigureExport.cppm
        Runtime.ObjectSpaceNormalBakeBinding.cppm
        Runtime.ObjectSpaceNormalBakeQueue.cppm
        Runtime.ObjectSpaceNormalBakeService.cppm
        Runtime.ObjectSpaceNormalBakeSubmission.cppm
        Runtime.ProgressivePoissonGpuBackend.cppm
        Runtime.ProgressivePresentationExtraction.cppm
        Runtime.ProgressiveRenderData.cppm
        Runtime.RenderArtifactPublication.cppm
        Runtime.SandboxConfigSections.cppm
        Runtime.SelectedMeshTextureBake.cppm
        Runtime.EcsSystemBundle.cppm
        Runtime.EditorCommandHistory.cppm
        Editor/Runtime.EditorPropertyWidgets.cppm
        Editor/Runtime.EditorUiHost.cppm
        Editor/Runtime.EditorUiModule.cppm
        Editor/Runtime.EditorWindowRegistry.cppm
        Runtime.Engine.cppm
        Runtime.EngineConfigBoot.cppm
        Runtime.EngineConfigControl.cppm
        Runtime.FramePacingDiagnostics.cppm
        Runtime.InputActions.cppm
        Runtime.KernelEvents.cppm
        Runtime.Module.cppm
        Runtime.ModuleSchedule.cppm
        Runtime.ServiceRegistry.cppm
        Runtime.WorldHandle.cppm
        Runtime.WorldRegistry.cppm
        Runtime.GeometryAvailability.cppm
        Runtime.GpuReadbackJob.cppm
        Runtime.KMeansBackend.cppm
        Runtime.KMeansGpuBackend.cppm
        Runtime.RegistrationAlignment.cppm
        Runtime.PhysicsBridge.cppm
        Scene/Runtime.SceneDocumentModule.cppm
        Scene/Runtime.SceneInteractionModule.cppm
        Runtime.SceneSerialization.cppm
        Cameras/Runtime.CameraModule.cppm
        Cameras/Runtime.CameraControllers.cppm
        Cameras/Runtime.CameraFocusCommand.cppm
        Runtime.SandboxEditorFacades.cppm
        Gizmos/Runtime.GizmoFrameService.cppm
        Gizmos/Runtime.GizmoInteraction.cppm
        ImGui/Runtime.ImGuiAdapter.cppm
        Runtime.GraphGeometryPacker.cppm
        Runtime.MeshGeometryPacker.cppm
        Runtime.MeshPrimitiveViewPacker.cppm
        Runtime.PointCloudGeometryPacker.cppm
        Runtime.PrimitiveSelectionRefinement.cppm
        Runtime.ProceduralGeometry.cppm
        Runtime.ProceduralGeometryPacker.cppm
        Runtime.ReferenceScene.cppm
        Runtime.RenderRecipeActivation.cppm
        Runtime.RenderExtraction.cppm
        Runtime.RenderWorldPool.cppm
        Runtime.SelectionReadback.cppm
        Runtime.SelectionController.cppm
        Runtime.StableEntityLookup.cppm
        Runtime.StreamingExecutor.cppm
        Runtime.TextureBakeModule.cppm
        Runtime.VertexAttributeBinding.cppm
        Runtime.VertexChannelBindings.cppm
        Runtime.VertexChannelStreams.cppm
        SpatialDebug/Runtime.SpatialDebugAdapters.cppm
        SpatialDebug/Runtime.SpatialDebugClosestFace.cppm
        Visualization/Runtime.VisualizationAdapters.cppm
        PRIVATE
        FILE_SET render_extraction_impl TYPE CXX_MODULES FILES
        Runtime.RenderExtraction.Internal.cpp
        PRIVATE
        Runtime.AsyncWorkModule.cpp
        Runtime.AsyncBufferReadback.cpp
        Runtime.AssetWorkflowModule.cpp
        Runtime.AssetImportPipeline.cpp
        Runtime.AssetGeometryIO.cpp
        Runtime.AssetIngestStateMachine.cpp
        Runtime.AssetMeshNormals.cpp
        Runtime.AssetModelSceneHandoff.cpp
        Runtime.AssetModelTextureHandoff.cpp
        Runtime.AssetModelTextureIO.cpp
        Runtime.CommandBus.cpp
        Runtime.DerivedJobGraph.cpp
        Runtime.DeviceBootstrap.cpp
        Runtime.JobService.cpp
        Runtime.JobServiceGpuQueueBridge.cpp
        Modules/Clustering/Runtime.ClusteringModule.cpp
        Runtime.MeshAttributeTextureBake.cpp
        Runtime.MethodFigureExport.cpp
        Runtime.ObjectSpaceNormalBakeBinding.cpp
        Runtime.ObjectSpaceNormalBakeQueue.cpp
        Runtime.ObjectSpaceNormalBakeService.cpp
        Runtime.ObjectSpaceNormalBakeSubmission.cpp
        Runtime.ProgressivePoissonGpuBackend.cpp
        Runtime.ProgressivePresentationExtraction.cpp
        Runtime.ProgressiveRenderData.cpp
        Runtime.RenderArtifactPublication.cpp
        Runtime.SandboxDefaultPolicies.cpp
        Runtime.SandboxConfigSections.cpp
        Runtime.SelectedMeshTextureBake.cpp
        Runtime.EcsSystemBundle.cpp
        Runtime.EditorCommandHistory.cpp
        Editor/Runtime.EditorPropertyWidgets.cpp
        Editor/Runtime.EditorUiHost.cpp
        Editor/Runtime.EditorUiModule.cpp
        Editor/Runtime.EditorWindowRegistry.cpp
        Runtime.Engine.cpp
        Runtime.EngineConfigBoot.cpp
        Runtime.EngineConfigControl.cpp
        Runtime.FramePacingDiagnostics.cpp
        Runtime.InputActions.cpp
        Runtime.KernelEvents.cpp
        Runtime.ModuleSchedule.cpp
        Runtime.ServiceRegistry.cpp
        Runtime.WorldRegistry.cpp
        Runtime.GeometryAvailability.cpp
        Runtime.GpuReadbackJob.cpp
        Runtime.KMeansBackend.cpp
        Runtime.KMeansGpuBackend.cpp
        Runtime.KMeansGpuJobQueue.cpp
        Runtime.RegistrationAlignment.cpp
        Runtime.PhysicsBridge.cpp
        Scene/Runtime.SceneDocumentModule.cpp
        Scene/Runtime.SceneInteractionModule.cpp
        Runtime.SceneSerialization.cpp
        Cameras/Runtime.CameraModule.cpp
        Cameras/Runtime.CameraControllers.cpp
        Cameras/Runtime.CameraFocusCommand.cpp
        Runtime.SandboxMethodFacade.cpp
        Runtime.SandboxParameterizationFacade.cpp
        Runtime.SandboxEditorRenderRecipeFacade.cpp
        Runtime.SandboxEditorFacades.cpp
        Gizmos/Runtime.GizmoFrameService.cpp
        Gizmos/Runtime.GizmoInteraction.cpp
        ImGui/Runtime.ImGuiAdapter.cpp
        Runtime.GraphGeometryPacker.cpp
        Runtime.MeshGeometryPacker.cpp
        Runtime.MeshPrimitiveViewPacker.cpp
        Runtime.PointCloudGeometryPacker.cpp
        Runtime.PrimitiveSelectionRefinement.cpp
        Runtime.ProceduralGeometry.cpp
        Runtime.ProceduralGeometryPacker.cpp
        Runtime.ReferenceScene.cpp
        Runtime.RenderRecipeActivation.cpp
        Runtime.RenderExtraction.cpp
        Runtime.RenderExtraction.Adapters.cpp
        Runtime.RenderExtraction.Geometry.cpp
        Runtime.RenderExtractionService.cpp
        Runtime.RenderWorldPool.cpp
        Runtime.SelectionReadback.cpp
        Runtime.SelectionController.cpp
        Runtime.StableEntityLookup.cpp
        Runtime.StreamingExecutor.cpp
        Runtime.TextureBakeModule.cpp
        Runtime.VertexAttributeBinding.cpp
        Runtime.VertexChannelStreams.cpp
        SpatialDebug/Runtime.SpatialDebugAdapters.cpp
        SpatialDebug/Runtime.SpatialDebugClosestFace.cpp
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
        ExtrinsicPhysics
        ExtrinsicPlatform
        IntrinsicGeometry
        EnTT::EnTT
        glm::glm
)

# RUNTIME-090: the runtime-side Dear ImGui adapter links the backend-agnostic
# ImGui core PRIVATE so `imgui.h` (and its transitive deps) stay out of the
# runtime's public module surface. `imgui_core_lib` is always available, so the
# adapter builds in headless-no-glfw configs too.
target_link_libraries(${target_name} PRIVATE
        IntrinsicProgressivePoissonReference
        imgui_core_lib
        implot::implot
)

# ASSETIO-001 Slice C.2b keeps concrete model/texture decoder dependencies
# runtime-private. The exported registration seam only mentions promoted asset
# bridge types, so tinygltf/stb headers and symbols do not leak into assets.
target_link_libraries(${target_name} PRIVATE tinygltf stb nlohmann_json::nlohmann_json)

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
