// Private editor bindings, shared decision/mutation rules and workspace state support the UI surfaces.
module;

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <entt/entity/registry.hpp>
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
import Extrinsic.ECS.Component.Transform;
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
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.InputActions;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.MeshPrimitiveView;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryAvailability;
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

    inline constexpr std::uint64_t kEditorSignatureOffset = 1469598103934665603ull;

    void MixSignature(std::uint64_t& signature,
                      std::uint64_t value) noexcept;

    void MixSignatureString(std::uint64_t& signature,
                            const std::string_view value) noexcept;

    [[nodiscard]] std::uint64_t GeometryMetadataSignatureForEntity(
        const entt::registry& raw,
        const ECS::EntityHandle entity);

    [[nodiscard]] std::optional<ECS::EntityHandle> ResolveStableEntity(
        const entt::registry& raw,
        const std::uint32_t stableId);

    [[nodiscard]] bool SameTransformComponent(
        const ECS::Components::Transform::Component& lhs,
        const ECS::Components::Transform::Component& rhs) noexcept;

    [[nodiscard]] EditorCommandHistoryResult ExecuteEditorTransformMutation(
        EditorCommandHistory& history,
        ECS::Scene::Registry* scene,
        const WorldHandle world,
        const std::uint32_t stableEntityId,
        const ECS::Components::Transform::Component& before,
        const ECS::Components::Transform::Component& after,
        std::string label);

    [[nodiscard]] EditorCommandStatus ToEditorCommandStatus(
        const EditorCommandHistoryStatus status) noexcept;

    [[nodiscard]] std::string BuildImportSuccessMessage(
        const EditorFileImportCommand& command,
        const EditorFileImportResult& result);

    [[nodiscard]] std::string BuildImportPendingMessage(
        const EditorFileImportCommand& command,
        const Assets::AssetPayloadKind payloadKind);

    [[nodiscard]] std::string BuildImportFailureMessage(
        const Core::ErrorCode error);

    [[nodiscard]] std::string BuildSceneFileSuccessMessage(
        const EditorSceneFileCommand& command,
        const EditorSceneFileResult& result);

    [[nodiscard]] std::string BuildSceneFileFailureMessage(
        const EditorSceneFileOperation operation,
        const Core::ErrorCode error);

    [[nodiscard]] std::string BuildSceneFilePendingMessage(
        const EditorSceneFileCommand& command,
        const EditorSceneFileOperation operation);

    [[nodiscard]] EditorJobModel ToEditorJobModel(
        const EditorJobRecord& job);

    using EditorModelBuildClock = std::chrono::steady_clock;

    class ScopedEditorStatTimer final
    {
    public:
        explicit ScopedEditorStatTimer(std::uint64_t* target) noexcept;
        ScopedEditorStatTimer(const ScopedEditorStatTimer&) = delete;
        ScopedEditorStatTimer& operator=(const ScopedEditorStatTimer&) = delete;
        ~ScopedEditorStatTimer();

    private:
        std::uint64_t* m_Target{nullptr};
        EditorModelBuildClock::time_point m_Start{};
    };

    struct FileImportPrerequisiteEvaluation
    {
        bool CanChoosePayloadHint{false};
        bool CanImport{false};
        Assets::AssetPayloadKind ResolvedPayloadKind{
            Assets::AssetPayloadKind::Unknown};
        std::array<EditorFileImportPayloadOption, 6> PayloadOptions{};
        std::string PayloadHintDisabledReason{};
        std::string ImportDisabledReason{};
        Core::ErrorCode Error{Core::ErrorCode::Success};
    };

    [[nodiscard]] FileImportPrerequisiteEvaluation
    EvaluateFileImportPrerequisites(
        const bool commandSurfaceAvailable,
        const std::string_view path,
        const Assets::AssetPayloadKind selectedPayloadKind);

    [[nodiscard]] bool IsInternalVisualizationProperty(
        const std::string& name) noexcept;

    [[nodiscard]] bool IsConnectivityVisualizationProperty(
        const std::string& name) noexcept;

    [[nodiscard]] GeometryElementDomain ToGeometryElementDomain(
        const EditorVisualizationPropertyDomain domain) noexcept;

    [[nodiscard]] GeometryElementDomain ToGeometryElementDomain(
        const EditorPropertyCatalogDomain domain) noexcept;

    [[nodiscard]] const Geometry::PropertySet* PropertySetForVisualizationDomain(
        const GeometryEntityAvailability& availability,
        const EditorVisualizationPropertyDomain domain) noexcept;

    void AppendVisualizationPropertiesForDomain(
        std::vector<EditorVisualizationPropertyInfo>& out,
        const Geometry::PropertySet& properties,
        const EditorVisualizationPropertyDomain domain);

    [[nodiscard]] const Geometry::PropertySet* PropertySetForCatalogDomain(
        const GeometryEntityAvailability& availability,
        const EditorPropertyCatalogDomain domain) noexcept;

    [[nodiscard]] bool IsPropertyCatalogSupportedKind(
        const Geometry::PropertyValueKind kind) noexcept;

    [[nodiscard]] std::optional<EditorPropertyCatalogDomain>
    VertexChannelCatalogDomainForView(
        const ECS::Components::GeometrySources::ConstSourceView& view) noexcept;

    [[nodiscard]] const Geometry::PropertySet*
    VertexChannelPropertySetForView(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        const EditorPropertyCatalogDomain domain) noexcept;

    [[nodiscard]] std::optional<AttributeSourceType>
    ToAttributeSourceType(
        const Geometry::PropertyValueKind kind) noexcept;

    [[nodiscard]] bool SourceTypeAllowedForVertexChannel(
        const VertexChannel channel,
        const AttributeSourceType type) noexcept;

    [[nodiscard]] AttributeBindResult EvaluateVertexChannelBinding(
        const Geometry::PropertySet& properties,
        const VertexChannel channel,
        const std::string_view propertyName,
        const AttributeSourceType sourceType,
        const std::size_t elementCount,
        EditorWorkspaceSnapshotStats* modelBuildStats);

    [[nodiscard]] std::uint64_t EditorElapsedNs(
        const EditorModelBuildClock::time_point start) noexcept;

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
        SpatialIndexCache* SpatialIndices{};
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
        const EditorNormalEstimationResult* LastNormalEstimationResult{nullptr};
        const EditorOutlierAnalysisResult* LastOutlierAnalysisResult{nullptr};
        const EditorKernelDensityResult* LastKernelDensityResult{nullptr};
        const EditorPointSpacingResult* LastPointSpacingResult{nullptr};
        const EditorBilateralFilterResult* LastBilateralFilterResult{nullptr};
        const EditorKeypointAnalysisResult* LastKeypointAnalysisResult{nullptr};
        const EditorDescriptorAnalysisResult* LastDescriptorAnalysisResult{nullptr};
        const EditorDensityWeightResult* LastDensityWeightResult{nullptr};
        const EditorPointConstructionResult* LastPointConstructionResult{nullptr};
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
        SpatialIndexCache* m_SpatialIndices{};
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
        std::optional<EditorNormalEstimationResult> m_LastNormalEstimationResult{};
        std::optional<EditorOutlierAnalysisResult> m_LastOutlierAnalysisResult{};
        std::optional<EditorKernelDensityResult> m_LastKernelDensityResult{};
        std::optional<EditorPointSpacingResult> m_LastPointSpacingResult{};
        std::optional<EditorBilateralFilterResult> m_LastBilateralFilterResult{};
        std::optional<EditorKeypointAnalysisResult> m_LastKeypointAnalysisResult{};
        std::optional<EditorDescriptorAnalysisResult> m_LastDescriptorAnalysisResult{};
        std::optional<EditorDensityWeightResult> m_LastDensityWeightResult{};
        std::optional<EditorPointConstructionResult> m_LastPointConstructionResult{};
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

    [[nodiscard]] bool SameRenderHintComponent(
        const std::optional<Graphics::Components::RenderSurface>& lhs,
        const std::optional<Graphics::Components::RenderSurface>& rhs);
    [[nodiscard]] bool SameRenderHintComponent(
        const std::optional<Graphics::Components::RenderEdges>& lhs,
        const std::optional<Graphics::Components::RenderEdges>& rhs);
    [[nodiscard]] bool SameRenderHintComponent(
        const std::optional<Graphics::Components::RenderPoints>& lhs,
        const std::optional<Graphics::Components::RenderPoints>& rhs);

} // namespace Extrinsic::Runtime::EditorFeatureDetail
