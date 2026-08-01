set(target_name ExtrinsicRuntime)
intrinsic_add_module_library(${target_name})

option(INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN
        "Allow runtime to instantiate the promoted fail-closed Vulkan IDevice when RenderConfig opts in"
        ON)

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Runtime.AsyncWorkModule.cppm
        Runtime.AssetWorkflowModule.cppm
        Runtime.AssetIngestStateMachine.cppm
        Runtime.CommandBus.cppm
        Runtime.DeviceBootstrap.cppm
        Runtime.JobService.cppm
        Runtime.JobServiceGpuQueueBridge.cppm
        Modules/Clustering/Runtime.ClusteringModule.cppm
        Modules/Clustering/Runtime.ClusteringGpuBackend.cppm
        Runtime.GeometryPresentation.cppm
        Runtime.ProgressivePoissonGpuBackend.cppm
        Runtime.RenderArtifactPublication.cppm
        Modules/Clustering/Runtime.ClusteringConfig.cppm
        Runtime.ProgressivePoissonConfig.cppm
        Runtime.ParameterizationConfig.cppm
        Runtime.EcsSystemBundle.cppm
        Runtime.EditorCommandHistory.cppm
        Runtime.EditorCommon.cppm
        Runtime.EditorWorkspaceSnapshots.cppm
        Runtime.EditorJobProjection.cppm
        Runtime.SceneEditingOperations.cppm
        Runtime.GeometryProcessingOperations.cppm
        Runtime.VisualizationEditingOperations.cppm
        Runtime.RenderRecipeEditingOperations.cppm
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
        Runtime.PhysicsBridge.cppm
        Scene/Runtime.SceneDocumentModule.cppm
        Scene/Runtime.SceneInteractionModule.cppm
        Runtime.SceneSerialization.cppm
        Cameras/Runtime.CameraModule.cppm
        Cameras/Runtime.CameraControllers.cppm
        Cameras/Runtime.CameraFocusCommand.cppm
        Gizmos/Runtime.GizmoFrameService.cppm
        Gizmos/Runtime.GizmoInteraction.cppm
        ImGui/Runtime.ImGuiAdapter.cppm
        Runtime.MeshPrimitiveView.cppm
        Runtime.MeshSurfaceTopology.cppm
        Runtime.PrimitiveSelectionRefinement.cppm
        Runtime.ReferenceScene.cppm
        Runtime.RenderRecipeActivation.cppm
        Runtime.RenderExtraction.cppm
        Runtime.RenderWorldPool.cppm
        Runtime.SelectionReadback.cppm
        Runtime.SelectionController.cppm
        Runtime.StableEntityLookup.cppm
        Runtime.TextureBakeModule.cppm
        Runtime.VertexAttributeBinding.cppm
        Runtime.VertexChannelBindings.cppm
        Runtime.VertexChannelStreams.cppm
        Visualization/Runtime.VisualizationRecipes.cppm
        PRIVATE
        FILE_SET feature_config_impl TYPE CXX_MODULES FILES
        internal/Runtime.FeatureConfigCodecs.Detail.cppm
        PRIVATE
        FILE_SET editor_feature_impl TYPE CXX_MODULES FILES
        internal/Runtime.EditorFeatures.Detail.cppm
        PRIVATE
        FILE_SET asset_workflow_impl TYPE CXX_MODULES FILES
        Runtime.AssetWorkflowImportExecutor.cppm
        Runtime.AssetWorkflowGeometryMaterialization.cppm
        Runtime.AssetWorkflowModelMaterialization.cppm
        Runtime.AssetWorkflowModelTextureDecode.cppm
        Runtime.AssetWorkflowRecipePolicies.cppm
        Runtime.AssetWorkflowTextureResidency.cppm
        PRIVATE
        FILE_SET render_extraction_impl TYPE CXX_MODULES FILES
        Runtime.GeometryPlanBuilders.cppm
        Runtime.RenderExtraction.Internal.cpp
        PRIVATE
        Runtime.AsyncWorkModule.cpp
        Runtime.AssetWorkflowModule.cpp
        Runtime.AssetWorkflowImportExecutor.cpp
        Runtime.AssetIngestStateMachine.cpp
        Runtime.AssetWorkflowGeometryMaterialization.cpp
        Runtime.AssetWorkflowModelMaterialization.cpp
        Runtime.AssetWorkflowModelTextureDecode.cpp
        Runtime.AssetWorkflowRecipePolicies.cpp
        Runtime.AssetWorkflowTextureResidency.cpp
        Runtime.CommandBus.cpp
        Runtime.DeviceBootstrap.cpp
        Runtime.JobService.cpp
        Runtime.JobServiceGpuQueueBridge.cpp
        Modules/Clustering/Runtime.ClusteringModule.cpp
        Modules/Clustering/Runtime.ClusteringGpuBackend.cpp
        Modules/Clustering/Runtime.ClusteringGpuState.cpp
        Runtime.GeometryPresentation.cpp
        Runtime.ProgressivePoissonGpuBackend.cpp
        Runtime.RenderArtifactPublication.cpp
        Modules/Clustering/Runtime.ClusteringConfig.cpp
        Runtime.ProgressivePoissonConfig.cpp
        Runtime.ParameterizationConfig.cpp
        internal/Runtime.FeatureConfigCodecs.Detail.cpp
        Runtime.EcsSystemBundle.cpp
        Runtime.EditorCommandHistory.cpp
        Runtime.EditorCommon.Public.cpp
        Runtime.EditorJobProjection.Public.cpp
        Runtime.SceneEditingOperations.Public.cpp
        Runtime.GeometryProcessingOperations.Public.cpp
        Runtime.VisualizationEditingOperations.Public.cpp
        Runtime.RenderRecipeEditingOperations.Public.cpp
        Runtime.EditorWorkspaceSnapshots.Public.cpp
        internal/Runtime.EditorFeatureContextAdapters.cpp
        internal/Runtime.EditorWorkspaceSnapshots.cpp
        Runtime.GeometryProcessingOperations.cpp
        Runtime.ParameterizationOperations.cpp
        Runtime.RenderRecipeEditingOperations.cpp
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
        Runtime.PhysicsBridge.cpp
        Scene/Runtime.SceneDocumentModule.cpp
        Scene/Runtime.SceneInteractionModule.cpp
        Runtime.SceneSerialization.cpp
        Cameras/Runtime.CameraModule.cpp
        Cameras/Runtime.CameraControllers.cpp
        Cameras/Runtime.CameraFocusCommand.cpp
        Gizmos/Runtime.GizmoFrameService.cpp
        Gizmos/Runtime.GizmoInteraction.cpp
        ImGui/Runtime.ImGuiAdapter.cpp
        Runtime.GeometryPlanBuilders.Graph.cpp
        Runtime.GeometryPlanBuilders.Mesh.cpp
        Runtime.GeometryPlanBuilders.MeshPrimitiveView.cpp
        Runtime.GeometryPlanBuilders.PointCloud.cpp
        Runtime.GeometryPlanBuilders.Procedural.cpp
        Runtime.MeshSurfaceTopology.cpp
        Runtime.PrimitiveSelectionRefinement.cpp
        Runtime.ReferenceScene.cpp
        Runtime.RenderRecipeActivation.cpp
        Runtime.RenderExtraction.cpp
        Runtime.RenderExtraction.Recipes.cpp
        Runtime.RenderExtraction.Geometry.cpp
        Runtime.RenderExtractionService.cpp
        Runtime.RenderWorldPool.cpp
        Runtime.SelectionReadback.cpp
        Runtime.SelectionController.cpp
        Runtime.StableEntityLookup.cpp
        Runtime.TextureBakeModule.cpp
        Runtime.VertexAttributeBinding.cpp
        Runtime.VertexChannelStreams.cpp
        Visualization/Runtime.VisualizationRecipes.cpp
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
