set(target_name ExtrinsicRuntime)
intrinsic_add_module_library(${target_name})

option(INTRINSIC_RUNTIME_ENABLE_PROMOTED_VULKAN
        "Allow runtime to instantiate the promoted fail-closed Vulkan IDevice when RenderConfig opts in"
        ON)

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Kernel/Runtime.AsyncWorkModule.cppm
        AssetWorkflow/Runtime.AssetWorkflowModule.cppm
        AssetWorkflow/Runtime.AssetIngestStateMachine.cppm
        Kernel/Runtime.CommandBus.cppm
        Kernel/Runtime.DeviceBootstrap.cppm
        Kernel/Runtime.JobService.cppm
        Modules/Clustering/Runtime.ClusteringModule.cppm
        Modules/Clustering/Runtime.ClusteringGpuBackend.cppm
        GeometryIntegration/Runtime.GeometryPresentation.cppm
        Modules/ProgressivePoisson/Runtime.ProgressivePoissonGpuBackend.cppm
        Rendering/Runtime.RenderArtifactPublication.cppm
        Modules/Clustering/Runtime.ClusteringConfig.cppm
        Modules/CurvatureSegmentation/Runtime.CurvatureSegmentationConfig.cppm
        Modules/ProgressivePoisson/Runtime.ProgressivePoissonConfig.cppm
        Modules/Parameterization/Runtime.ParameterizationConfig.cppm
        Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationConfig.cppm
        Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationModule.cppm
        Editor/Runtime.EditorCommandHistory.cppm
        Editor/Runtime.EditorCommon.cppm
        Editor/Runtime.EditorWorkspaceAttachment.cppm
        Editor/Runtime.EditorWorkspaceSnapshots.cppm
        Editor/Runtime.EditorJobProjection.cppm
        Editor/Operations/Runtime.SceneEditingOperations.cppm
        Editor/Operations/Runtime.GeometryProcessingOperations.cppm
        Editor/Operations/Runtime.VisualizationEditingOperations.cppm
        Editor/Operations/Runtime.RenderRecipeEditingOperations.cppm
        Editor/Runtime.EditorPropertyWidgets.cppm
        Editor/Runtime.EditorUiHost.cppm
        Editor/Runtime.EditorUiModule.cppm
        Editor/Runtime.EditorWindowRegistry.cppm
        Kernel/Runtime.Engine.cppm
        Config/Runtime.EngineConfigBoot.cppm
        Config/Runtime.EngineConfigControl.cppm
        Kernel/Runtime.FramePacingDiagnostics.cppm
        Kernel/Runtime.InputActions.cppm
        Kernel/Runtime.KernelEvents.cppm
        Kernel/Runtime.Module.cppm
        Kernel/Runtime.ServiceRegistry.cppm
        Kernel/Runtime.WorldHandle.cppm
        Kernel/Runtime.WorldRegistry.cppm
        GeometryIntegration/Runtime.GeometryAvailability.cppm
        Modules/PhysicsIntegration/Runtime.PhysicsModule.cppm
        Scene/Runtime.SceneDocumentModule.cppm
        Scene/Runtime.SceneInteractionModule.cppm
        Scene/Runtime.SceneSerialization.cppm
        Cameras/Runtime.CameraModule.cppm
        Cameras/Runtime.CameraControllers.cppm
        Cameras/Runtime.CameraFocusCommand.cppm
        Gizmos/Runtime.GizmoInteraction.cppm
        ImGui/Runtime.ImGuiAdapter.cppm
        GeometryIntegration/Runtime.MeshPrimitiveView.cppm
        GeometryIntegration/Runtime.MeshSurfaceTopology.cppm
        GeometryIntegration/Runtime.PrimitiveSelectionRefinement.cppm
        Scene/Runtime.ReferenceScene.cppm
        Config/Runtime.RenderRecipeActivation.cppm
        Rendering/Runtime.RenderExtraction.cppm
        Rendering/Runtime.RenderWorldPool.cppm
        Scene/Runtime.SelectionController.cppm
        Scene/Runtime.StableEntityLookup.cppm
        Modules/TextureBake/Runtime.TextureBakeModule.cppm
        GeometryIntegration/Runtime.VertexAttributeBinding.cppm
        GeometryIntegration/Runtime.VertexChannelBindings.cppm
        GeometryIntegration/Runtime.VertexChannelStreams.cppm
        Visualization/Runtime.VisualizationRecipes.cppm
        PRIVATE
        FILE_SET feature_config_impl TYPE CXX_MODULES FILES
        Config/internal/Runtime.FeatureConfigCodecs.Detail.cppm
        PRIVATE
        FILE_SET editor_feature_impl TYPE CXX_MODULES FILES
        Editor/internal/Runtime.EditorFeatures.Detail.cppm
        Editor/internal/Runtime.EditorWorkspaceAttachment.Detail.cppm
        PRIVATE
        FILE_SET asset_workflow_impl TYPE CXX_MODULES FILES
        AssetWorkflow/Runtime.AssetWorkflowImportExecutor.cppm
        AssetWorkflow/Runtime.AssetWorkflowGeometryMaterialization.cppm
        AssetWorkflow/Runtime.AssetWorkflowModelMaterialization.cppm
        AssetWorkflow/Runtime.AssetWorkflowModelTextureDecode.cppm
        AssetWorkflow/Runtime.AssetWorkflowRecipePolicies.cppm
        AssetWorkflow/Runtime.AssetWorkflowTextureResidency.cppm
        PRIVATE
        FILE_SET render_extraction_impl TYPE CXX_MODULES FILES
        GeometryIntegration/Runtime.GeometryPlanBuilders.cppm
        Rendering/Runtime.RenderExtraction.Internal.cpp
        PRIVATE
        Kernel/Runtime.AsyncWorkModule.cpp
        AssetWorkflow/Runtime.AssetWorkflowModule.cpp
        AssetWorkflow/Runtime.AssetWorkflowImportExecutor.cpp
        AssetWorkflow/Runtime.AssetIngestStateMachine.cpp
        AssetWorkflow/Runtime.AssetWorkflowGeometryMaterialization.cpp
        AssetWorkflow/Runtime.AssetWorkflowModelMaterialization.cpp
        AssetWorkflow/Runtime.AssetWorkflowModelTextureDecode.cpp
        AssetWorkflow/Runtime.AssetWorkflowRecipePolicies.cpp
        AssetWorkflow/Runtime.AssetWorkflowTextureResidency.cpp
        Kernel/Runtime.CommandBus.cpp
        Kernel/Runtime.DeviceBootstrap.cpp
        Kernel/Runtime.JobService.cpp
        Modules/Clustering/Runtime.ClusteringModule.cpp
        Modules/Clustering/Runtime.ClusteringGpuBackend.cpp
        Modules/Clustering/Runtime.ClusteringGpuState.cpp
        GeometryIntegration/Runtime.GeometryPresentation.cpp
        Modules/ProgressivePoisson/Runtime.ProgressivePoissonGpuBackend.cpp
        Rendering/Runtime.RenderArtifactPublication.cpp
        Modules/Clustering/Runtime.ClusteringConfig.cpp
        Modules/CurvatureSegmentation/Runtime.CurvatureSegmentationConfig.cpp
        Modules/ProgressivePoisson/Runtime.ProgressivePoissonConfig.cpp
        Modules/Parameterization/Runtime.ParameterizationConfig.cpp
        Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationConfig.cpp
        Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationModule.cpp
        Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationGpu.cpp
        Config/internal/Runtime.FeatureConfigCodecs.Detail.cpp
        Editor/Runtime.EditorCommandHistory.cpp
        Editor/Runtime.EditorCommon.Public.cpp
        Editor/Runtime.EditorWorkspaceAttachment.cpp
        Editor/Runtime.EditorJobProjection.Public.cpp
        Editor/Operations/Runtime.SceneEditingOperations.Public.cpp
        Editor/Operations/Runtime.SceneEditingOperations.Actions.cpp
        Editor/Operations/Runtime.SceneEditingOperations.Debug.cpp
        Editor/Operations/Runtime.GeometryProcessingOperations.Public.cpp
        Editor/Operations/Runtime.VisualizationEditingOperations.Public.cpp
        Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp
        Editor/Operations/Runtime.VisualizationEditingOperations.Debug.cpp
        Editor/Operations/Runtime.RenderRecipeEditingOperations.Public.cpp
        Editor/Runtime.EditorWorkspaceSnapshots.Public.cpp
        Editor/Runtime.EditorWorkspaceSnapshots.Models.cpp
        Editor/internal/Runtime.EditorFeatureContextAdapters.cpp
        Editor/internal/Runtime.EditorWorkspaceSession.cpp
        Editor/Operations/Runtime.GeometryProcessingOperations.cpp
        Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp
        Editor/Operations/Runtime.ParameterizationOperations.cpp
        Editor/Operations/Runtime.RenderRecipeEditingOperations.cpp
        Editor/Operations/Runtime.RenderRecipeEditingOperations.Debug.cpp
        Editor/Runtime.EditorPropertyWidgets.cpp
        Editor/Runtime.EditorUiHost.cpp
        Editor/Runtime.EditorUiModule.cpp
        Editor/Runtime.EditorWindowRegistry.cpp
        Kernel/Runtime.Engine.cpp
        Config/Runtime.EngineConfigBoot.cpp
        Config/Runtime.EngineConfigControl.cpp
        Kernel/Runtime.FramePacingDiagnostics.cpp
        Kernel/Runtime.InputActions.cpp
        Kernel/Runtime.KernelEvents.cpp
        Kernel/Runtime.ServiceRegistry.cpp
        Kernel/Runtime.WorldRegistry.cpp
        GeometryIntegration/Runtime.GeometryAvailability.cpp
        Modules/PhysicsIntegration/Runtime.PhysicsModule.cpp
        Scene/Runtime.SceneDocumentModule.cpp
        Scene/Runtime.SceneInteractionModule.cpp
        Scene/Runtime.SceneSerialization.cpp
        Cameras/Runtime.CameraModule.cpp
        Cameras/Runtime.CameraControllers.cpp
        Cameras/Runtime.CameraFocusCommand.cpp
        Gizmos/Runtime.GizmoInteraction.cpp
        ImGui/Runtime.ImGuiAdapter.cpp
        GeometryIntegration/Runtime.GeometryPlanBuilders.Graph.cpp
        GeometryIntegration/Runtime.GeometryPlanBuilders.Mesh.cpp
        GeometryIntegration/Runtime.GeometryPlanBuilders.MeshPrimitiveView.cpp
        GeometryIntegration/Runtime.GeometryPlanBuilders.PointCloud.cpp
        GeometryIntegration/Runtime.GeometryPlanBuilders.Procedural.cpp
        GeometryIntegration/Runtime.MeshSurfaceTopology.cpp
        GeometryIntegration/Runtime.PrimitiveSelectionRefinement.cpp
        Scene/Runtime.ReferenceScene.cpp
        Config/Runtime.RenderRecipeActivation.cpp
        Rendering/Runtime.RenderExtraction.cpp
        Rendering/Runtime.RenderExtraction.Recipes.cpp
        Rendering/Runtime.RenderExtraction.Geometry.cpp
        Rendering/Runtime.RenderWorldPool.cpp
        Scene/Runtime.SelectionController.cpp
        Scene/Runtime.StableEntityLookup.cpp
        Modules/TextureBake/Runtime.TextureBakeModule.cpp
        GeometryIntegration/Runtime.VertexAttributeBinding.cpp
        GeometryIntegration/Runtime.VertexChannelStreams.cpp
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
