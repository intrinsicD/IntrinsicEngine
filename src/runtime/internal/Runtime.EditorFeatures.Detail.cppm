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
    [[nodiscard]] Geometry::ConstPropertySet
    ResolveEditorSelectedMeshVertexPropertiesImpl(const EditorFeatureBindings& context);
    EditorCommandStatus
    ApplyEditorCameraControllerCommandImpl(const EditorFeatureBindings& context,
                                           const EditorCameraControllerCommand& command);
    EditorSceneFileResult ApplyEditorCloseSceneCommandImpl(const EditorFeatureBindings& context);
    [[nodiscard]] RuntimeEngineConfigApplyResult
    ApplyEditorClusteringConfigImpl(const EditorFeatureBindings& context,
                                    const ClusteringConfig& config,
                                    std::string sourceId = "sandbox.clustering");
    EditorParameterizationResult ApplyEditorConfiguredParameterizationCommandImpl(
        const EditorFeatureBindings& context,
        const EditorConfiguredParameterizationCommand& command);
    EditorFileImportResult ApplyEditorFileImportCommandImpl(const EditorFeatureBindings& context,
                                                            const EditorFileImportCommand& command);
    EditorCommandStatus ApplyEditorGeometryPresentationSlotDefaultCommandImpl(
        const EditorFeatureBindings& context,
        const EditorGeometryPresentationSlotDefaultCommand& command);
    EditorCommandStatus ApplyEditorGeometryPresentationSlotPropertyCommandImpl(
        const EditorFeatureBindings& context,
        const EditorGeometryPresentationSlotPropertyCommand& command);

    [[nodiscard]] EditorGpuProfilingConfigResult ApplyEditorGpuProfilingConfigCommandImpl(
        const EditorFeatureBindings& context, bool enabled,
        std::string sourceId = "sandbox.frame_graph.gpu_profiling");
    EditorGraphVertexNormalsResult
    ApplyEditorGraphVertexNormalsCommandImpl(const EditorFeatureBindings& context,
                                             const EditorGraphVertexNormalsCommand& command);
    EditorMeshCurvatureResult
    ApplyEditorMeshCurvatureCommandImpl(const EditorFeatureBindings& context,
                                        const EditorMeshCurvatureCommand& command);
    EditorMeshDenoiseResult
    ApplyEditorMeshDenoiseCommandImpl(const EditorFeatureBindings& context,
                                      const EditorMeshDenoiseCommand& command);
    EditorMeshRemeshResult ApplyEditorMeshRemeshCommandImpl(const EditorFeatureBindings& context,
                                                            const EditorMeshRemeshCommand& command);
    EditorMeshSimplifyResult
    ApplyEditorMeshSimplifyCommandImpl(const EditorFeatureBindings& context,
                                       const EditorMeshSimplifyCommand& command);
    EditorMeshSubdivideResult
    ApplyEditorMeshSubdivideCommandImpl(const EditorFeatureBindings& context,
                                        const EditorMeshSubdivideCommand& command);
    EditorMeshVertexNormalsResult
    ApplyEditorMeshVertexNormalsCommandImpl(const EditorFeatureBindings& context,
                                            const EditorMeshVertexNormalsCommand& command);
    EditorSceneFileResult ApplyEditorNewSceneCommandImpl(const EditorFeatureBindings& context);
    EditorParameterizationResult
    ApplyEditorParameterizationCommandImpl(const EditorFeatureBindings& context,
                                           const EditorParameterizationCommand& command);
    [[nodiscard]] EditorParameterizationConfigResult ApplyEditorParameterizationConfigCommandImpl(
        const EditorFeatureBindings& context, const EditorParameterizationConfigCommand& command);
    EditorPointCloudOutlierRemovalResult ApplyEditorPointCloudOutlierRemovalCommandImpl(
        const EditorFeatureBindings& context, const EditorPointCloudOutlierRemovalCommand& command);
    EditorPointCloudVertexNormalsResult ApplyEditorPointCloudVertexNormalsCommandImpl(
        const EditorFeatureBindings& context, const EditorPointCloudVertexNormalsCommand& command);
    EditorCommandStatus
    ApplyEditorPrimitiveViewCommandImpl(const EditorFeatureBindings& context,
                                        const EditorPrimitiveViewCommand& command);
    EditorProgressivePoissonResult
    ApplyEditorProgressivePoissonCommandImpl(const EditorFeatureBindings& context,
                                             const EditorProgressivePoissonCommand& command);
    [[nodiscard]] EditorProgressivePoissonConfigResult
    ApplyEditorProgressivePoissonConfigCommandImpl(
        const EditorFeatureBindings& context, const EditorProgressivePoissonConfigCommand& command);
    EditorRegistrationResult
    ApplyEditorRegistrationCommandImpl(const EditorFeatureBindings& context,
                                       const EditorRegistrationCommand& command);
    EditorCommandStatus ApplyEditorRenderHintCommandImpl(const EditorFeatureBindings& context,
                                                         const EditorRenderHintCommand& command);
    EditorRenderRecipeCommandResult
    ApplyEditorRenderRecipeCommandImpl(const EditorFeatureBindings& context,
                                       const EditorRenderRecipeCommand& command);
    EditorSceneFileResult ApplyEditorSceneLoadCommandImpl(const EditorFeatureBindings& context,
                                                          const EditorSceneFileCommand& command);
    EditorSceneFileResult ApplyEditorSceneSaveCommandImpl(const EditorFeatureBindings& context,
                                                          const EditorSceneFileCommand& command);
    EditorTextureBakeCommandResult
    ApplyEditorTextureBakeCommandImpl(const EditorFeatureBindings& context,
                                      const EditorTextureBakeCommand& command);
    EditorCommandStatus ApplyEditorTransformEditImpl(const EditorFeatureBindings& context,
                                                     const EditorTransformEditCommand& command);
    EditorUvRegenerationCommandResult
    ApplyEditorUvRegenerationCommandImpl(const EditorFeatureBindings& context,
                                         const EditorUvRegenerationCommand& command);
    EditorCommandStatus
    ApplyEditorVertexChannelBindingCommandImpl(const EditorFeatureBindings& context,
                                               const EditorVertexChannelBindingCommand& command);
    EditorCommandStatus
    ApplyEditorVisualizationConfigCommandImpl(const EditorFeatureBindings& context,
                                              const EditorVisualizationConfigCommand& command);
    EditorCommandStatus
    ApplyEditorVisualizationPropertyCommandImpl(const EditorFeatureBindings& context,
                                                const EditorVisualizationPropertyCommand& command);
    EditorCommandStatus
    ApplyEditorVisualizationRecipeCommandImpl(const EditorFeatureBindings& context,
                                              const EditorVisualizationRecipeCommand& command);
    [[nodiscard]] EditorDomainWindowModel
    BuildEditorDomainWindowModelImpl(const EditorFeatureBindings& context,
                                     EditorDomainWindowKind kind);
    [[nodiscard]] EditorParameterizationViewModel
    BuildEditorParameterizationViewModelImpl(const EditorFeatureBindings& context);
    [[nodiscard]] EditorRenderRecipeEditorModel
    BuildEditorRenderRecipeEditorModelImpl(const EditorFeatureBindings& context);
    [[nodiscard]] EditorWorkspaceSnapshot
    BuildEditorWorkspaceSnapshotImpl(const EditorFeatureBindings& context);
    [[nodiscard]] EditorWorkspaceSnapshot
    BuildEditorWorkspaceSnapshotImpl(const EditorFeatureBindings& context,
                                     const EditorWorkspaceSnapshotRequest& request);
    [[nodiscard]] const char*
    DebugNameForEditorAssetPayloadKindImpl(EditorAssetPayloadKind kind) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorBoundRenderStateRowKindImpl(EditorBoundRenderStateRowKind kind) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorCameraControllerKindImpl(Core::Config::CameraControllerKind kind) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorCommandStatusImpl(EditorCommandStatus status) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorDiagnosticCodeImpl(EditorDiagnosticCode code) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorDomainWindowKindImpl(EditorDomainWindowKind kind) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorGeometryDomainImpl(ECS::Components::GeometrySources::Domain domain) noexcept;
    [[nodiscard]] const char* DebugNameForEditorGeometryProcessingAlgorithmImpl(
        EditorGeometryProcessingAlgorithm algorithm) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorGeometryProcessingDomainImpl(EditorGeometryProcessingDomain domain) noexcept;
    [[nodiscard]] const char* DebugNameForEditorICPVariantImpl(EditorICPVariant variant) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorMeshCurvatureOutputImpl(EditorMeshCurvatureOutput output) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorMeshRemeshModeImpl(EditorMeshRemeshMode mode) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorMeshRemeshSizingLawImpl(EditorMeshRemeshSizingLaw sizingLaw) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorMeshSimplifyMetricImpl(EditorMeshSimplifyMetric metric) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorMeshSubdivideOperatorImpl(EditorMeshSubdivideOperator op) noexcept;
    [[nodiscard]] const char* DebugNameForEditorParameterizationUvViewStatusImpl(
        EditorParameterizationUvViewStatus status) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorPrimitiveKindImpl(RefinedPrimitiveKind kind) noexcept;
    [[nodiscard]] const char* DebugNameForEditorProgressivePoissonBackendImpl(
        EditorProgressivePoissonBackend backend) noexcept;
    [[nodiscard]] const char* DebugNameForEditorProgressivePoissonChannelImpl(
        EditorProgressivePoissonChannel channel) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorPropertyCatalogDomainImpl(EditorPropertyCatalogDomain domain) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorRenderRecipeCommandKindImpl(EditorRenderRecipeCommandKind kind) noexcept;
    [[nodiscard]] const char* DebugNameForEditorRenderRecipeCommandStatusImpl(
        EditorRenderRecipeCommandStatus status) noexcept;
    [[nodiscard]] std::string_view DebugNameForEditorRenderRecipeConfigDiagnosticCodeImpl(
        EditorRenderRecipeConfigDiagnosticCode code) noexcept;
    [[nodiscard]] std::string_view
    DebugNameForEditorRenderRecipeConfigStateImpl(EditorRenderRecipeConfigState state) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorRenderRecipeDraftStateImpl(EditorRenderRecipeDraftState state) noexcept;
    [[nodiscard]] const char* DebugNameForEditorUvAtlasProvenanceImpl(
        Geometry::UvAtlas::UvAtlasProvenance provenance) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorUvAtlasStatusImpl(Geometry::UvAtlas::UvAtlasStatus status) noexcept;
    [[nodiscard]] const char* DebugNameForEditorVisualizationColorSourceImpl(
        Graphics::Components::VisualizationConfig::ColorSource source) noexcept;
    [[nodiscard]] const char* DebugNameForEditorVisualizationDomainImpl(
        Graphics::Components::VisualizationConfig::Domain domain) noexcept;
    [[nodiscard]] const char* DebugNameForEditorVisualizationPropertyDomainImpl(
        EditorVisualizationPropertyDomain domain) noexcept;
    [[nodiscard]] const char* DebugNameForEditorVisualizationPropertyPresetImpl(
        EditorVisualizationPropertyPreset preset) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorVisualizationRecipeKindImpl(VisualizationRecipeKind kind) noexcept;
    [[nodiscard]] const char*
    DebugNameForEditorVisualizationTargetImpl(EditorVisualizationTarget target) noexcept;
    void DisableEditorParameterizationUvViewImpl(const EditorFeatureBindings& context);
    [[nodiscard]] std::vector<EditorGeometryProcessingDomain>
    GetAvailableEditorKMeansDomainsImpl(const ECS::Scene::Registry& registry,
                                        ECS::EntityHandle entity);
    [[nodiscard]] std::optional<ClusteringConfig>
    GetEditorClusteringConfigImpl(const EditorFeatureBindings& context) noexcept;
    [[nodiscard]] EditorGeometryProcessingCapabilities
    GetEditorGeometryProcessingCapabilitiesImpl(const ECS::Scene::Registry& registry,
                                                ECS::EntityHandle entity);
    [[nodiscard]] std::vector<EditorGeometryProcessingMenuItem>
    GetEditorGeometryProcessingMenuItemsImpl(EditorDomainWindowKind kind);
    [[nodiscard]] std::optional<ParameterizationConfig>
    GetEditorParameterizationConfigImpl(const EditorFeatureBindings& context) noexcept;
    [[nodiscard]] std::optional<EditorProgressivePoissonConfig>
    GetEditorProgressivePoissonConfigImpl(const EditorFeatureBindings& context) noexcept;
    [[nodiscard]] EditorGeometryProcessingDomain GetEditorSupportedGeometryProcessingDomainsImpl(
        EditorGeometryProcessingAlgorithm algorithm) noexcept;
    [[nodiscard]] bool
    SupportsEditorGeometryProcessingDomainImpl(EditorGeometryProcessingAlgorithm algorithm,
                                               EditorGeometryProcessingDomain domain) noexcept;
    [[nodiscard]] bool IsActiveEditorJobStateImpl(JobState state) noexcept;
    [[nodiscard]] bool IsEditorTextureBakeTargetCompatibleImpl(
        const EditorTextureBakeTarget& target, Geometry::PropertyValueKind valueKind,
        PropertyTextureBakeStorage storage, PropertyTextureBakeEncoding encoding) noexcept;
    [[nodiscard]] bool IsFailedEditorJobStateImpl(JobState state) noexcept;
    [[nodiscard]] EditorProgressivePoissonBackend
    MakeEditorProgressivePoissonBackendImpl(ProgressivePoissonPlaygroundBackend backend) noexcept;
    [[nodiscard]] EditorProgressivePoissonChannel
    MakeEditorProgressivePoissonChannelImpl(ProgressivePoissonPlaygroundChannel channel) noexcept;
    [[nodiscard]] EditorProgressivePoissonConfig MakeEditorProgressivePoissonConfigImpl(
        const ProgressivePoissonPlaygroundConfig& config) noexcept;
    TextureBakeMutationResult RemoveEditorBakedTextureImpl(const EditorFeatureBindings& context,
                                                           std::uint32_t stableEntityId,
                                                           std::string_view outputName);

    TextureBakeMutationResult RenameEditorBakedTextureImpl(const EditorFeatureBindings& context,
                                                           std::uint32_t stableEntityId,
                                                           std::string_view currentName,
                                                           std::string_view newName);
    [[nodiscard]] std::vector<EditorGeometryProcessingEntry>
    ResolveEditorGeometryProcessingEntriesImpl(EditorGeometryProcessingCapabilities capabilities);
    [[nodiscard]] std::vector<EditorGeometryProcessingEntry>
    ResolveEditorGeometryProcessingEntriesImpl(const ECS::Scene::Registry& registry,
                                               ECS::EntityHandle entity);
    [[nodiscard]] Geometry::ConstPropertySet
    ResolveEditorSelectedMeshVertexPropertiesImpl(const EditorFeatureBindings& context);
    [[nodiscard]] PropertyTextureBakeRepresentation
    ResolveEditorTextureBakeTargetRepresentationImpl(
        Geometry::PropertyValueKind valueKind, PropertyTextureBakeStorage requestedStorage,
        PropertyTextureBakeEncoding requestedEncoding,
        std::span<const EditorTextureBakeTarget> targets) noexcept;
    [[nodiscard]] bool SameEditorJobOutputImpl(const EditorJobIdentity& lhs,
                                               const EditorJobIdentity& rhs) noexcept;
    bool SelectEditorEntityImpl(const EditorFeatureBindings& context, std::uint32_t stableEntityId);
    TextureBakeMutationResult
    SetEditorBakedTextureTargetsImpl(const EditorFeatureBindings& context,
                                     const EditorTextureBakeTargetUpdateRequest& request);
    [[nodiscard]] std::string_view StableTokenForEditorParameterizationStrategyImpl(
        EditorParameterizationStrategy strategy) noexcept;

    [[nodiscard]] EditorParameterizationUvViewState
    SubmitEditorParameterizationUvViewImpl(const EditorFeatureBindings& context,
                                           const EditorParameterizationViewModel& model,
                                           std::uint32_t width, std::uint32_t height);
    [[nodiscard]] EditorJobScope ToEditorJobScopeImpl(GeometryElementDomain domain) noexcept;

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
