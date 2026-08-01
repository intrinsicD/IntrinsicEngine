#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.RenderRecipeActivation;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.WorldHandle;

namespace Intrinsic::Tests
{
    namespace Assets = Extrinsic::Assets;
    namespace Core = Extrinsic::Core;
    namespace ECS = Extrinsic::ECS;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Runtime = Extrinsic::Runtime;

    // Explicit low-level test seam for feature operations. Production app
    // code receives bound command handles and copied snapshots instead of
    // assembling these live runtime dependencies.
    struct EditorFeatureTestContext
    {
        ECS::Scene::Registry* Scene{nullptr};
        Runtime::WorldHandle World{Runtime::DefaultWorldHandle};
        Runtime::SelectionController* Selection{nullptr};
        Runtime::EditorCommandHistory* CommandHistory{nullptr};
        Assets::AssetService* AssetService{nullptr};
        const std::optional<Runtime::PrimitiveSelectionResult>* LastRefinedPrimitive{nullptr};
        std::uint64_t LastRefinedPrimitiveGeneration{0u};
        Runtime::CameraControllerRegistry* CameraControllers{nullptr};
        Core::Extent2D CameraViewport{};
        RHI::IDevice* Device{nullptr};
        Runtime::TextureBakeService* TextureBake{nullptr};
        Runtime::ClusteringService* Clustering{nullptr};
        Runtime::EditorAssetImportCommandSurface AssetImportCommands{};
        Runtime::EditorAssetImportQueueCommandSurface AssetImportQueueCommands{};
        Runtime::EditorSceneFileCommandSurface SceneFileCommands{};
        Runtime::EditorPrimitiveViewCommandSurface PrimitiveViewCommands{};
        Runtime::EditorParameterizationUvViewCommandSurface ParameterizationUvViewCommands{};
        Runtime::EditorVisualizationRecipeCommandSurface VisualizationRecipes{};
        std::uint64_t VisualizationRecipeRevision{0u};
        Runtime::EditorJobCommandSurface JobCommands{};
        Runtime::EditorMethodResultSinks MethodResultSinks{};
        Runtime::RuntimeAssetImportQueueSnapshot AssetImportQueue{};
        std::string PendingAssetImportPath{};
        std::string PendingSceneFilePath{};
        Assets::AssetPayloadKind PendingAssetImportPayloadKind{Assets::AssetPayloadKind::Unknown};
        const Runtime::EditorFileImportResult* LastAssetImportResult{nullptr};
        const Runtime::EditorSceneFileResult* LastSceneFileResult{nullptr};
        const Runtime::KMeansRunCompleted* LastKMeansResult{nullptr};
        const Runtime::EditorMeshDenoiseResult* LastMeshDenoiseResult{nullptr};
        const Runtime::EditorMeshCurvatureResult* LastMeshCurvatureResult{nullptr};
        const Runtime::EditorMeshRemeshResult* LastMeshRemeshResult{nullptr};
        const Runtime::EditorMeshSubdivideResult* LastMeshSubdivideResult{nullptr};
        const Runtime::EditorMeshSimplifyResult* LastMeshSimplifyResult{nullptr};
        const Runtime::EditorMeshVertexNormalsResult* LastMeshVertexNormalsResult{nullptr};
        const Runtime::EditorGraphVertexNormalsResult* LastGraphVertexNormalsResult{nullptr};
        const Runtime::EditorPointCloudVertexNormalsResult* LastPointCloudVertexNormalsResult{
            nullptr};
        const Runtime::EditorPointCloudOutlierRemovalResult* LastPointCloudOutlierRemovalResult{
            nullptr};
        const Runtime::EditorUvRegenerationCommandResult* LastUvRegenerationResult{nullptr};
        const Runtime::EditorParameterizationResult* LastParameterizationResult{nullptr};
        const Runtime::EditorProgressivePoissonResult* LastProgressivePoissonResult{nullptr};
        const Runtime::EditorRegistrationResult* LastRegistrationResult{nullptr};
        const Graphics::RenderGraphFrameStats* RenderGraphStats{nullptr};
        const Graphics::RenderRecipeConfigContext* RenderRecipeContext{nullptr};
        Runtime::EditorRenderRecipeEditorState* RenderRecipeEditorState{nullptr};
        const Runtime::RuntimeRenderRecipeState* RenderRecipeRuntimeState{nullptr};
        const Runtime::RuntimeEngineConfigControlState* EngineConfigControlState{nullptr};
        Runtime::EditorWorkspaceSnapshotStats* ModelBuildStats{nullptr};
        Runtime::EditorSelectedModelCache* SelectedModelCache{nullptr};
        std::function<bool()> AttachmentActive{};
        std::function<Graphics::RenderRecipeConfigLoadResult(const std::string&,
                                                             const std::string&)>
            PreviewRenderRecipeDocument{};
        std::function<Runtime::RuntimeRenderRecipeApplyResult(
            const Graphics::RenderRecipeConfigLoadResult&)>
            ApplyRenderRecipePreview{};
        std::function<Core::Config::EngineConfigLoadResult(const std::string&, const std::string&)>
            PreviewEngineConfigDocument{};
        std::function<Runtime::RuntimeEngineConfigApplyResult(
            const Core::Config::EngineConfigLoadResult&)>
            ApplyEngineConfigHotSubset{};
        Runtime::RenderArtifactRegistry* RenderArtifacts{nullptr};
        bool ImGuiAdapterAvailable{false};
        bool AssetImportCommandsAvailable{false};
        bool SceneFileCommandsAvailable{false};
        bool CameraRenderCommandsAvailable{false};
        bool VisualizationCommandsAvailable{false};
        bool RenderRecipeCommandsAvailable{false};
        bool EngineConfigCommandsAvailable{false};
        bool MeshDenoiseKernelAvailable{true};
        bool MeshCurvatureKernelAvailable{true};
        bool MeshCurvatureDirectionsAvailable{true};
        bool MeshRemeshUniformKernelAvailable{true};
        bool MeshRemeshAdaptiveKernelAvailable{true};
        bool MeshRemeshProjectToSurfaceAvailable{true};
        bool MeshRemeshErrorBoundedSizingAvailable{true};
        bool MeshSubdivideLoopKernelAvailable{true};
        bool MeshSubdivideCatmullClarkKernelAvailable{true};
        bool MeshSubdivideSqrt3KernelAvailable{true};
        bool MeshSubdivideLoopFeatureEdgesAvailable{true};
        bool MeshSimplifyKernelAvailable{true};

        [[nodiscard]] std::function<void()> MakeWorkspaceSnapshotCacheInvalidator() const
        {
            Runtime::EditorSelectedModelCache* const cache = SelectedModelCache;
            if (cache == nullptr)
                return {};
            return [cache] { cache->Clear(); };
        }

        [[nodiscard]] operator Runtime::EditorSceneEditingContext() const
        {
            return Runtime::EditorSceneEditingContext{
                .Scene = Scene,
                .World = World,
                .Selection = Selection,
                .CommandHistory = CommandHistory,
                .AssetService = AssetService,
                .LastRefinedPrimitive = LastRefinedPrimitive,
                .LastRefinedPrimitiveGeneration = LastRefinedPrimitiveGeneration,
                .CameraControllers = CameraControllers,
                .CameraViewport = CameraViewport,
                .AssetImportCommands = AssetImportCommands,
                .AssetImportQueueCommands = AssetImportQueueCommands,
                .SceneFileCommands = SceneFileCommands,
                .PrimitiveViewCommands = PrimitiveViewCommands,
                .AssetImportQueue = AssetImportQueue,
                .PendingAssetImportPath = PendingAssetImportPath,
                .PendingSceneFilePath = PendingSceneFilePath,
                .PendingAssetImportPayloadKind = PendingAssetImportPayloadKind,
                .LastAssetImportResult = LastAssetImportResult,
                .LastSceneFileResult = LastSceneFileResult,
                .AttachmentActive = AttachmentActive,
                .InvalidateWorkspaceSnapshotCache = MakeWorkspaceSnapshotCacheInvalidator(),
                .ImGuiAdapterAvailable = ImGuiAdapterAvailable,
                .AssetImportCommandsAvailable = AssetImportCommandsAvailable,
                .SceneFileCommandsAvailable = SceneFileCommandsAvailable,
                .CameraRenderCommandsAvailable = CameraRenderCommandsAvailable,
            };
        }

        [[nodiscard]] operator Runtime::EditorGeometryProcessingContext() const
        {
            return Runtime::EditorGeometryProcessingContext{
                .Scene = Scene,
                .World = World,
                .Selection = Selection,
                .CommandHistory = CommandHistory,
                .Device = Device,
                .Clustering = Clustering,
                .ParameterizationUvViewCommands = ParameterizationUvViewCommands,
                .JobCommands = JobCommands,
                .MethodResultSinks = MethodResultSinks,
                .LastKMeansResult = LastKMeansResult,
                .LastMeshDenoiseResult = LastMeshDenoiseResult,
                .LastMeshCurvatureResult = LastMeshCurvatureResult,
                .LastMeshRemeshResult = LastMeshRemeshResult,
                .LastMeshSubdivideResult = LastMeshSubdivideResult,
                .LastMeshSimplifyResult = LastMeshSimplifyResult,
                .LastMeshVertexNormalsResult = LastMeshVertexNormalsResult,
                .LastGraphVertexNormalsResult = LastGraphVertexNormalsResult,
                .LastPointCloudVertexNormalsResult = LastPointCloudVertexNormalsResult,
                .LastPointCloudOutlierRemovalResult = LastPointCloudOutlierRemovalResult,
                .LastUvRegenerationResult = LastUvRegenerationResult,
                .LastParameterizationResult = LastParameterizationResult,
                .LastProgressivePoissonResult = LastProgressivePoissonResult,
                .LastRegistrationResult = LastRegistrationResult,
                .EngineConfigControlState = EngineConfigControlState,
                .PreviewEngineConfigDocument = PreviewEngineConfigDocument,
                .ApplyEngineConfigHotSubset = ApplyEngineConfigHotSubset,
                .AttachmentActive = AttachmentActive,
                .InvalidateWorkspaceSnapshotCache = MakeWorkspaceSnapshotCacheInvalidator(),
                .EngineConfigCommandsAvailable = EngineConfigCommandsAvailable,
                .MeshDenoiseKernelAvailable = MeshDenoiseKernelAvailable,
                .MeshCurvatureKernelAvailable = MeshCurvatureKernelAvailable,
                .MeshCurvatureDirectionsAvailable = MeshCurvatureDirectionsAvailable,
                .MeshRemeshUniformKernelAvailable = MeshRemeshUniformKernelAvailable,
                .MeshRemeshAdaptiveKernelAvailable = MeshRemeshAdaptiveKernelAvailable,
                .MeshRemeshProjectToSurfaceAvailable = MeshRemeshProjectToSurfaceAvailable,
                .MeshRemeshErrorBoundedSizingAvailable = MeshRemeshErrorBoundedSizingAvailable,
                .MeshSubdivideLoopKernelAvailable = MeshSubdivideLoopKernelAvailable,
                .MeshSubdivideCatmullClarkKernelAvailable =
                    MeshSubdivideCatmullClarkKernelAvailable,
                .MeshSubdivideSqrt3KernelAvailable = MeshSubdivideSqrt3KernelAvailable,
                .MeshSubdivideLoopFeatureEdgesAvailable = MeshSubdivideLoopFeatureEdgesAvailable,
                .MeshSimplifyKernelAvailable = MeshSimplifyKernelAvailable,
            };
        }

        [[nodiscard]] operator Runtime::EditorVisualizationEditingContext() const
        {
            return Runtime::EditorVisualizationEditingContext{
                .Scene = Scene,
                .World = World,
                .Selection = Selection,
                .CommandHistory = CommandHistory,
                .TextureBake = TextureBake,
                .VisualizationRecipes = VisualizationRecipes,
                .VisualizationRecipeRevision = VisualizationRecipeRevision,
                .JobCommands = JobCommands,
                .ModelBuildStats = ModelBuildStats,
                .AttachmentActive = AttachmentActive,
                .InvalidateWorkspaceSnapshotCache = MakeWorkspaceSnapshotCacheInvalidator(),
                .VisualizationCommandsAvailable = VisualizationCommandsAvailable,
            };
        }

        [[nodiscard]] operator Runtime::EditorRenderRecipeEditingContext() const
        {
            return Runtime::EditorRenderRecipeEditingContext{
                .RenderGraphStats = RenderGraphStats,
                .RenderRecipeContext = RenderRecipeContext,
                .RenderRecipeEditorState = RenderRecipeEditorState,
                .RenderRecipeRuntimeState = RenderRecipeRuntimeState,
                .PreviewRenderRecipeDocument = PreviewRenderRecipeDocument,
                .ApplyRenderRecipePreview = ApplyRenderRecipePreview,
                .EngineConfigControlState = EngineConfigControlState,
                .PreviewEngineConfigDocument = PreviewEngineConfigDocument,
                .ApplyEngineConfigHotSubset = ApplyEngineConfigHotSubset,
                .RenderArtifacts = RenderArtifacts,
                .AttachmentActive = AttachmentActive,
                .RenderRecipeCommandsAvailable = RenderRecipeCommandsAvailable,
                .EngineConfigCommandsAvailable = EngineConfigCommandsAvailable,
            };
        }

        [[nodiscard]] operator Runtime::EditorWorkspaceSnapshotContext() const
        {
            return Runtime::EditorWorkspaceSnapshotContext{
                .Scene = static_cast<Runtime::EditorSceneEditingContext>(*this),
                .Geometry = static_cast<Runtime::EditorGeometryProcessingContext>(*this),
                .Visualization = static_cast<Runtime::EditorVisualizationEditingContext>(*this),
                .RenderRecipe = static_cast<Runtime::EditorRenderRecipeEditingContext>(*this),
                .SelectedModelCache = SelectedModelCache,
            };
        }
    };
} // namespace Intrinsic::Tests
