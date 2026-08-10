module;

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module Extrinsic.Runtime.Private.EditorFeatures;

import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Core.StrongHandle;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.InputActions;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.MeshPrimitiveView;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.ProgressivePoissonConfig;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SceneSerialization;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.Graph.Vertex.Normals;
import Geometry.HalfedgeMesh.Vertices.Normals;
import Geometry.PointCloud.Normals;
import Geometry.PointCloud.Utils;
import Geometry.Properties;
import Geometry.Smoothing;
import Geometry.UvAtlas;
import Geometry.Parameterization;

import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;

export namespace Extrinsic::Runtime::EditorFeatureDetail
{
    using namespace Extrinsic::Runtime;

    struct EditorFeatureBindings
    {
        ECS::Scene::Registry* Scene{nullptr};
        WorldHandle World{DefaultWorldHandle};
        SelectionController* Selection{nullptr};
        EditorCommandHistory* CommandHistory{nullptr};
        Assets::AssetService* AssetService{nullptr};
        const std::optional<PrimitiveSelectionResult>* LastRefinedPrimitive{nullptr};
        std::uint64_t LastRefinedPrimitiveGeneration{0u};
        CameraControllerRegistry* CameraControllers{nullptr};
        Core::Extent2D CameraViewport{};
        RHI::IDevice* Device{nullptr};
        TextureBakeService* TextureBake{nullptr};
        ClusteringService* Clustering{nullptr};
        PointCloudConsolidationService* PointCloudConsolidation{nullptr};
        EditorAssetImportCommandSurface AssetImportCommands{};
        EditorAssetImportQueueCommandSurface AssetImportQueueCommands{};
        EditorSceneFileCommandSurface SceneFileCommands{};
        EditorPrimitiveViewCommandSurface PrimitiveViewCommands{};
        EditorParameterizationUvViewCommandSurface ParameterizationUvViewCommands{};
        EditorVisualizationRecipeCommandSurface VisualizationRecipes{};
        std::uint64_t VisualizationRecipeRevision{0u};
        EditorJobCommandSurface JobCommands{};
        EditorMethodResultSinks MethodResultSinks{};
        RuntimeAssetImportQueueSnapshot AssetImportQueue{};
        std::string PendingAssetImportPath{};
        std::string PendingSceneFilePath{};
        Assets::AssetPayloadKind PendingAssetImportPayloadKind{Assets::AssetPayloadKind::Unknown};
        const EditorFileImportResult* LastAssetImportResult{nullptr};
        const EditorSceneFileResult* LastSceneFileResult{nullptr};
        const KMeansRunCompleted* LastKMeansResult{nullptr};
        const PointCloudConsolidationResult* LastPointCloudConsolidationResult{nullptr};
        const EditorMeshDenoiseResult* LastMeshDenoiseResult{nullptr};
        const EditorMeshCurvatureResult* LastMeshCurvatureResult{nullptr};
        const EditorMeshRemeshResult* LastMeshRemeshResult{nullptr};
        const EditorMeshSubdivideResult* LastMeshSubdivideResult{nullptr};
        const EditorMeshSimplifyResult* LastMeshSimplifyResult{nullptr};
        const EditorMeshVertexNormalsResult* LastMeshVertexNormalsResult{nullptr};
        const EditorGraphVertexNormalsResult* LastGraphVertexNormalsResult{nullptr};
        const EditorPointCloudVertexNormalsResult* LastPointCloudVertexNormalsResult{nullptr};
        const EditorPointCloudOutlierRemovalResult* LastPointCloudOutlierRemovalResult{nullptr};
        const EditorUvRegenerationCommandResult* LastUvRegenerationResult{nullptr};
        const EditorParameterizationResult* LastParameterizationResult{nullptr};
        const EditorProgressivePoissonResult* LastProgressivePoissonResult{nullptr};
        const EditorRegistrationResult* LastRegistrationResult{nullptr};
        const Graphics::RenderGraphFrameStats* RenderGraphStats{nullptr};
        const Graphics::RenderRecipeConfigContext* RenderRecipeContext{nullptr};
        EditorRenderRecipeEditorState* RenderRecipeEditorState{nullptr};
        const RuntimeRenderRecipeState* RenderRecipeRuntimeState{nullptr};
        const RuntimeEngineConfigControlState* EngineConfigControlState{nullptr};
        EditorWorkspaceSnapshotStats* ModelBuildStats{nullptr};
        EditorSelectedModelCache* SelectedModelCache{nullptr};
        std::function<bool()> AttachmentActive{};
        std::function<void()> InvalidateWorkspaceSnapshotCache{};
        std::function<Graphics::RenderRecipeConfigLoadResult(const std::string&,
                                                             const std::string&)>
            PreviewRenderRecipeDocument{};
        std::function<RuntimeRenderRecipeApplyResult(const Graphics::RenderRecipeConfigLoadResult&)>
            ApplyRenderRecipePreview{};
        std::function<Core::Config::EngineConfigLoadResult(const std::string&, const std::string&)>
            PreviewEngineConfigDocument{};
        std::function<RuntimeEngineConfigApplyResult(const Core::Config::EngineConfigLoadResult&)>
            ApplyEngineConfigHotSubset{};
        RenderArtifactRegistry* RenderArtifacts{nullptr};
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
        bool CurvatureSegmentationKernelAvailable{true};
        bool MeshRemeshUniformKernelAvailable{true};
        bool MeshRemeshAdaptiveKernelAvailable{true};
        bool MeshRemeshProjectToSurfaceAvailable{true};
        bool MeshRemeshErrorBoundedSizingAvailable{true};
        bool MeshSubdivideLoopKernelAvailable{true};
        bool MeshSubdivideCatmullClarkKernelAvailable{true};
        bool MeshSubdivideSqrt3KernelAvailable{true};
        bool MeshSubdivideLoopFeatureEdgesAvailable{true};
        bool MeshSimplifyKernelAvailable{true};
    };

    [[nodiscard]] EditorFeatureBindings
    ToEditorFeatureBindingsImpl(const EditorSceneEditingContext& context);
    [[nodiscard]] EditorFeatureBindings
    ToEditorFeatureBindingsImpl(const EditorGeometryProcessingContext& context);
    [[nodiscard]] EditorFeatureBindings
    ToEditorFeatureBindingsImpl(const EditorVisualizationEditingContext& context);
    [[nodiscard]] EditorFeatureBindings
    ToEditorFeatureBindingsImpl(const EditorRenderRecipeEditingContext& context);
    [[nodiscard]] EditorFeatureBindings
    ToEditorFeatureBindingsImpl(const EditorWorkspaceSnapshotContext& context);
    [[nodiscard]] EditorSceneEditingContext
    MakeEditorSceneEditingContext(const EditorFeatureBindings& bindings);
    [[nodiscard]] EditorGeometryProcessingContext
    MakeEditorGeometryProcessingContext(const EditorFeatureBindings& bindings);
    [[nodiscard]] EditorVisualizationEditingContext
    MakeEditorVisualizationEditingContext(const EditorFeatureBindings& bindings);
    [[nodiscard]] EditorRenderRecipeEditingContext
    MakeEditorRenderRecipeEditingContext(const EditorFeatureBindings& bindings);
    [[nodiscard]] EditorFeatureBindings MakeEditorFeatureBindings(
        WorldRegistry& worlds,
        ServiceRegistry& services);
    [[nodiscard]] EditorFileImportResult ProjectEditorFileImportResult(
        const RuntimeAssetImportEvent& event);
    [[nodiscard]] EditorSceneFileResult ProjectEditorSceneFileResult(
        const RuntimeSceneFileEvent& event);

    struct EditorWorkspacePreparedFrame
    {
        const EditorFeatureBindings& Context;
        const EditorWorkspaceSnapshot& Frame;
        std::optional<EditorFileImportResult>& LastAssetImportResult;
        std::optional<EditorSceneFileResult>& LastSceneFileResult;
        std::optional<EditorUvRegenerationCommandResult>& LastUvRegenerationResult;
    };

    using EditorWorkspacePreparedFrameVisitor = std::function<void(EditorWorkspacePreparedFrame)>;

    class EditorWorkspaceSession
    {
        public:
        EditorWorkspaceSession();
        ~EditorWorkspaceSession();

        EditorWorkspaceSession(const EditorWorkspaceSession&) = delete;
        EditorWorkspaceSession& operator=(const EditorWorkspaceSession&) = delete;
        EditorWorkspaceSession(EditorWorkspaceSession&&) = delete;
        EditorWorkspaceSession& operator=(EditorWorkspaceSession&&) = delete;

        void Attach(WorldRegistry& worlds, ServiceRegistry& services);
        void Detach();

        [[nodiscard]] bool PrepareFrame(
            const EditorWorkspaceSnapshotRequest& request = {},
            std::string pendingAssetImportPath = {},
            EditorAssetPayloadKind pendingAssetImportPayloadKind = EditorAssetPayloadKind::Unknown,
            std::string pendingSceneFilePath = {});

        // References in the prepared-frame view are valid only for the
        // duration of the visitor invocation.
        [[nodiscard]] bool VisitPreparedFrame(const EditorWorkspacePreparedFrameVisitor& visitor);

        [[nodiscard]] const EditorWorkspaceSnapshot& LastFrame() const noexcept
        {
            return m_LastFrame;
        }

        [[nodiscard]] bool IsAttached() const noexcept
        {
            return m_Worlds != nullptr && m_Services != nullptr;
        }

        private:
        void ResetAttachmentState();
        void DismissGeometryProcessingResult(
            EditorGeometryProcessingResultSlot slot);

        WorldRegistry* m_Worlds{nullptr};
        ServiceRegistry* m_Services{nullptr};
        JobService* m_Jobs{nullptr};
        bool m_FramePrepared{false};
        EditorFeatureBindings m_Context{};
        EditorWorkspaceSnapshot m_LastFrame{};
        EditorSelectedModelCache m_SelectedModelCache{};
        std::uint64_t m_LastObservedRuntimeImportSequence{0};
        std::uint64_t m_LastObservedRuntimeSceneFileSequence{0};
        std::optional<EditorFileImportResult> m_LastImportResult{};
        std::optional<EditorSceneFileResult> m_LastSceneFileResult{};
        std::optional<KMeansRunCompleted> m_LastKMeansResult{};
        ClusteringService* m_ClusteringService{};
        KernelEventSubscription m_KMeansCompletionSubscription{};
        PointCloudConsolidationService* m_PointCloudConsolidationService{};
        KernelEventSubscription m_PointCloudConsolidationCompletionSubscription{};
        std::optional<PointCloudConsolidationResult>
            m_LastPointCloudConsolidationResult{};
        std::optional<EditorMeshDenoiseResult> m_LastMeshDenoiseResult{};
        std::optional<EditorMeshCurvatureResult> m_LastMeshCurvatureResult{};
        std::optional<EditorMeshRemeshResult> m_LastMeshRemeshResult{};
        std::optional<EditorMeshSubdivideResult> m_LastMeshSubdivideResult{};
        std::optional<EditorMeshSimplifyResult> m_LastMeshSimplifyResult{};
        std::optional<EditorMeshVertexNormalsResult> m_LastMeshVertexNormalsResult{};
        std::optional<EditorGraphVertexNormalsResult> m_LastGraphVertexNormalsResult{};
        std::optional<EditorPointCloudVertexNormalsResult> m_LastPointCloudVertexNormalsResult{};
        std::optional<EditorPointCloudOutlierRemovalResult> m_LastPointCloudOutlierRemovalResult{};
        std::optional<EditorProgressivePoissonResult> m_LastProgressivePoissonResult{};
        std::optional<EditorUvRegenerationCommandResult> m_LastUvRegenerationResult{};
        std::optional<EditorParameterizationResult> m_LastParameterizationResult{};
        std::optional<EditorRegistrationResult> m_LastRegistrationResult{};
        // Submit-time identity for jobs this session put on `JobService`, which
        // stores none itself. The index is pruned against `SnapshotAll()` each
        // frame and projected by `EditorJobCommandSurface` queries.
        std::unordered_map<JobToken, EditorJobIdentity, Core::StrongHandleHash<JobTokenTag>>
            m_JobIdentities{};
        std::shared_ptr<std::atomic_bool> m_AttachmentEpoch{};
        Graphics::RenderRecipeConfigContext m_RenderRecipeContext{};
        EditorRenderRecipeEditorState m_RenderRecipeState{};
        RenderArtifactRegistry m_RenderArtifactRegistry{};
    };

} // namespace Extrinsic::Runtime::EditorFeatureDetail
