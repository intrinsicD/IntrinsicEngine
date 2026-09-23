module;
#include <span>
#include <array>
#include <cstddef>
#include <chrono>
#include <functional>
#include <string_view>
#include <entt/entity/fwd.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

module Extrinsic.Runtime.Private.EditorWorkspaceAttachment;


import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.Core.StrongHandle;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.ClusteringTypes;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PointCloudConsolidationTypes;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;
import Extrinsic.Runtime.VisualizationRecipes;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldHandle;
import Geometry.Properties;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;

#include "Editor/internal/Runtime.EditorFeatures.Internal.hpp"
#include "Editor/internal/Runtime.EditorPointInputReadiness.hpp"

namespace Extrinsic::Runtime::EditorFeatureDetail
{
    class EditorWorkspaceSession::Impl
    {
        public:
        Impl();
        ~Impl();

        Impl(const Impl&) = delete;
        Impl& operator=(const Impl&) = delete;
        Impl(Impl&&) = delete;
        Impl& operator=(Impl&&) = delete;

        void Attach(WorldRegistry& worlds, ServiceRegistry& services);
        void Detach();

        [[nodiscard]] bool PrepareFrame(
            const EditorWorkspaceSnapshotRequest& request,
            std::string pendingAssetImportPath,
            Assets::AssetPayloadKind pendingAssetImportPayloadKind,
            std::string pendingSceneFilePath);

        // References in the prepared-frame view are valid only for the
        // duration of the visitor invocation.
        [[nodiscard]] bool VisitPreparedFrame(const EditorWorkspacePreparedFrameVisitor& visitor);

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
        EditorProcessingContext m_ProcessingContext{};
        std::shared_ptr<EditorPointInputReadinessState> m_PointInputReadiness{};
        EditorFeatureResultBindings m_ResultBindings{};
        EditorWorkspaceSnapshot m_LastFrame{};
        EditorSelectedModelCache m_SelectedModelCache{};
        std::uint64_t m_LastObservedRuntimeImportSequence{0};
        std::uint64_t m_LastObservedRuntimeSceneFileSequence{0};
        std::optional<EditorFileImportResult> m_LastImportResult{};
        std::optional<EditorSceneFileResult> m_LastSceneFileResult{};
        EditorPointFieldResultsSnapshot m_PointFieldResults{};
        EditorPointAnalysisResultsSnapshot m_PointAnalysisResults{};
        EditorNormalResultsSnapshot m_NormalResults{};
        EditorRegistrationResultsSnapshot m_RegistrationResults{};
        EditorMeshFieldResultsSnapshot m_MeshFieldResults{};
        EditorMeshTopologyResultsSnapshot m_MeshTopologyResults{};
        EditorParameterizationResultsSnapshot m_ParameterizationResults{};
        EditorPointSetResultsSnapshot m_PointSetResults{};
        EditorPointConstructionResultsSnapshot m_PointConstructionResults{};
        EditorPointCloudServiceResultsSnapshot m_PointCloudServiceResults{};
        EditorPointFieldResultSinks m_PointFieldResultSinks{};
        EditorPointAnalysisResultSinks m_PointAnalysisResultSinks{};
        EditorNormalResultSinks m_NormalResultSinks{};
        EditorRegistrationResultSinks m_RegistrationResultSinks{};
        EditorMeshFieldResultSinks m_MeshFieldResultSinks{};
        EditorMeshTopologyResultSinks m_MeshTopologyResultSinks{};
        EditorParameterizationResultSinks m_ParameterizationResultSinks{};
        EditorPointSetResultSinks m_PointSetResultSinks{};
        EditorPointConstructionResultSinks m_PointConstructionResultSinks{};
        EditorPointCloudServiceResultSinks m_PointCloudServiceResultSinks{};
        EditorParameterizationUvViewCommandSurface m_ParameterizationUvViewCommands{};
        EditorPointCloudServiceBorrowedServices m_PointCloudServices{};
        SpatialIndexCache* m_SpatialIndices{};
        ClusteringService* m_ClusteringService{};
        KernelEventSubscription m_KMeansCompletionSubscription{};
        PointCloudConsolidationService* m_PointCloudConsolidationService{};
        KernelEventSubscription m_PointCloudConsolidationCompletionSubscription{};
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

    namespace
    {
        using EditorJobIdentityIndex =
            std::unordered_map<JobToken,
                               EditorJobIdentity,
                               Core::StrongHandleHash<JobTokenTag>>;

        [[nodiscard]] EditorJobRecord ToEditorJobRecord(
            const JobSnapshot& job,
            const EditorJobIdentity& identity)
        {
            return EditorJobRecord{
                .Token = job.Token,
                .Identity = identity,
                .Name = job.DebugName,
                .State = job.State,
                .NormalizedProgress = job.Progress.Normalized,
                .ProgressDeterminate = job.Progress.Determinate,
                .ElapsedMilliseconds = job.ElapsedMilliseconds,
            };
        }

        void PruneEditorJobIdentities(
            const std::vector<JobSnapshot>& jobs,
            EditorJobIdentityIndex& identities)
        {
            EditorJobIdentityIndex retained{};
            retained.reserve(identities.size());
            for (const JobSnapshot& job : jobs)
            {
                const auto identity = identities.find(job.Token);
                if (identity != identities.end())
                    retained.insert(*identity);
            }
            identities = std::move(retained);
        }

        [[nodiscard]] std::optional<EditorJobRecord>
        FindActiveEditorJob(
            const JobService& jobs,
            const EditorJobIdentityIndex& identities,
            const EditorJobIdentity& requested)
        {
            for (const JobSnapshot& job : jobs.SnapshotAll())
            {
                const auto identity = identities.find(job.Token);
                if (identity != identities.end() && IsActiveEditorJobState(job.State) &&
        SameEditorJobOutput(identity->second, requested))
                {
                    return ToEditorJobRecord(job, identity->second);
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::vector<EditorJobRecord>
        SnapshotEditorJobsForEntity(
            const JobService& jobs,
            const EditorJobIdentityIndex& identities,
            const std::uint32_t stableEntityId)
        {
            std::vector<EditorJobRecord> rows{};
            for (const JobSnapshot& job : jobs.SnapshotAll())
            {
                const auto identity = identities.find(job.Token);
                if (identity == identities.end() ||
                    identity->second.EntityId != stableEntityId)
                {
                    continue;
                }
                rows.push_back(ToEditorJobRecord(job, identity->second));
            }
            return rows;
        }
        [[nodiscard]] bool AttachmentEpochIsActive(
            const std::shared_ptr<std::atomic_bool>& epoch) noexcept
        {
            return epoch != nullptr &&
                epoch->load(std::memory_order_acquire);
        }

        template <typename Command, typename Fallback>
        [[nodiscard]] auto GuardAttachmentCommand(
            Command command,
            std::shared_ptr<std::atomic_bool> epoch,
            Fallback fallback)
        {
            return [command = std::move(command),
                    epoch = std::move(epoch),
                    fallback = std::move(fallback)](
                       auto&&... args) mutable -> decltype(auto)
            {
                if (!AttachmentEpochIsActive(epoch))
                {
                    return fallback(
                        std::forward<decltype(args)>(args)...);
                }
                return command(std::forward<decltype(args)>(args)...);
            };
        }

        void GuardParameterizationUvViewSurface(
            EditorParameterizationUvViewCommandSurface& surface,
            const std::shared_ptr<std::atomic_bool>& epoch)
        {
            if (surface.TextureTabs)
                surface.TextureTabs = GuardAttachmentCommand(
                    std::move(surface.TextureTabs), epoch,
                    [](std::uint32_t) { return std::vector<EditorParameterizationTextureTab>{}; });
            surface.Submit = GuardAttachmentCommand(
            std::move(surface.Submit), epoch,
            [](EditorParameterizationUvViewRequest request)
            {
                return EditorParameterizationUvViewState{
                    .Status =
                        EditorParameterizationUvViewStatus::CpuFallbackNonOperational,
                    .RequestedMode       = request.View.RenderMode,
                    .ActiveMode          = ParameterizationUvRenderMode::CpuLayout,
                    .RequestedBackground = request.View.BackgroundMode,
                    .ActiveBackground =
                        request.View.BackgroundMode == ParameterizationUvBackgroundMode::Grid ||
                                request.View.BackgroundMode ==
                                    ParameterizationUvBackgroundMode::Checker
                            ? request.View.BackgroundMode
                            : ParameterizationUvBackgroundMode::Checker,
                    .RequestToken = request.RequestToken,
                    .Width        = request.Width,
                    .Height       = request.Height,
                    .Message      = "GPU UV view command failed because the editor session "
                                    "attachment expired.",
                };
            });
        }

        void GuardAttachmentCommandSurfaces(
            EditorFeatureBindings& context,
            const std::shared_ptr<std::atomic_bool>& epoch)
        {
            context.AssetImportCommands.Import = GuardAttachmentCommand(
                std::move(context.AssetImportCommands.Import),
                epoch,
                [](const EditorFileImportCommand& command)
                {
                    return EditorFileImportResult{
                        .Status = EditorCommandStatus::AssetImportFailed,
                        .PayloadKind = command.PayloadKind,
                        .Error = Core::ErrorCode::InvalidState,
                        .Message =
                            "Asset import failed: editor session attachment expired.",
                    };
                });
            context.AssetImportQueueCommands.ClearCompleted =
                GuardAttachmentCommand(
                    std::move(
                        context.AssetImportQueueCommands.ClearCompleted),
                    epoch,
                    []()
                    {
                        return std::size_t{0u};
                    });
            context.AssetImportQueueCommands.Cancel = GuardAttachmentCommand(
                std::move(context.AssetImportQueueCommands.Cancel),
                epoch,
                [](const RuntimeAssetIngestHandle)
                {
                    return Core::Err(Core::ErrorCode::InvalidState);
                });
            context.SceneFileCommands.New = GuardAttachmentCommand(
                std::move(context.SceneFileCommands.New),
                epoch,
                []()
                {
                    return EditorSceneFileResult{
                        .Status = EditorCommandStatus::SceneNewFailed,
                        .Operation = EditorSceneFileOperation::New,
                        .Error = Core::ErrorCode::InvalidState,
                        .Message =
                            "New scene failed: editor session attachment expired.",
                    };
                });
            context.SceneFileCommands.Save = GuardAttachmentCommand(
                std::move(context.SceneFileCommands.Save),
                epoch,
                [](const EditorSceneFileCommand&)
                {
                    return EditorSceneFileResult{
                        .Status = EditorCommandStatus::SceneSaveFailed,
                        .Operation = EditorSceneFileOperation::Save,
                        .Error = Core::ErrorCode::InvalidState,
                        .Message =
                            "Scene save failed: editor session attachment expired.",
                    };
                });
            context.SceneFileCommands.Load = GuardAttachmentCommand(
                std::move(context.SceneFileCommands.Load),
                epoch,
                [](const EditorSceneFileCommand&)
                {
                    return EditorSceneFileResult{
                        .Status = EditorCommandStatus::SceneLoadFailed,
                        .Operation = EditorSceneFileOperation::Load,
                        .Error = Core::ErrorCode::InvalidState,
                        .Message =
                            "Scene load failed: editor session attachment expired.",
                    };
                });
            context.SceneFileCommands.Close = GuardAttachmentCommand(
                std::move(context.SceneFileCommands.Close),
                epoch,
                []()
                {
                    return EditorSceneFileResult{
                        .Status = EditorCommandStatus::SceneCloseFailed,
                        .Operation = EditorSceneFileOperation::Close,
                        .Error = Core::ErrorCode::InvalidState,
                        .Message =
                            "Scene close failed: editor session attachment expired.",
                    };
                });
            context.VisualizationRecipes.GetRecipe =
                GuardAttachmentCommand(
                    std::move(context.VisualizationRecipes.GetRecipe),
                    epoch,
                    [](const std::uint32_t)
                    {
                        return std::optional<VisualizationRecipe>{};
                    });
            context.VisualizationRecipes.SetRecipe =
                GuardAttachmentCommand(
                    std::move(context.VisualizationRecipes.SetRecipe),
                    epoch,
                    [](const std::uint32_t, VisualizationRecipe)
                    {
                    });
            context.VisualizationRecipes.ClearRecipe =
                GuardAttachmentCommand(
                    std::move(context.VisualizationRecipes.ClearRecipe),
                    epoch,
                    [](const std::uint32_t)
                    {
                    });
            if (context.PreviewRenderRecipeDocument)
            {
                context.PreviewRenderRecipeDocument =
                    GuardAttachmentCommand(
                        std::move(
                            context.PreviewRenderRecipeDocument),
                        epoch,
                        [](const std::string&, const std::string&)
                        {
                            return Graphics::
                                RenderRecipeConfigLoadResult{};
                        });
            }
            if (context.ApplyRenderRecipePreview)
            {
                context.ApplyRenderRecipePreview =
                    GuardAttachmentCommand(
                        std::move(
                            context.ApplyRenderRecipePreview),
                        epoch,
                        [](const Graphics::
                               RenderRecipeConfigLoadResult&)
                        {
                            return RuntimeRenderRecipeApplyResult{
                                .Status =
                                    RuntimeRenderRecipeApplyStatus::
                                        Rejected,
                            };
                        });
            }
        }

    } // namespace

    EditorWorkspaceSession::Impl::Impl()
    {
    }

    EditorWorkspaceSession::Impl::~Impl()
    {
        Detach();
    }

    void EditorWorkspaceSession::Impl::Attach(WorldRegistry& worlds, ServiceRegistry& services)
    {
        Detach();
        m_Worlds          = &worlds;
        m_Services        = &services;
        m_AttachmentEpoch = std::make_shared<std::atomic_bool>(true);
        m_Jobs = services.Find<JobService>();
        m_PointInputReadiness = MakeEditorPointInputReadiness(
            worlds, services.Find<CommandBus>(), m_Jobs);
        m_SpatialIndices = services.Find<SpatialIndexCache>();
        m_ClusteringService = services.Find<ClusteringService>();
        if (m_ClusteringService != nullptr &&
            m_ClusteringService->Available())
        {
            m_KMeansCompletionSubscription =
                m_ClusteringService->SubscribeRunCompleted(
                    [epoch = m_AttachmentEpoch, this](
                        const KMeansRunCompleted& completed)
                    {
                        if (!AttachmentEpochIsActive(epoch))
                            return;
                        m_PointCloudServiceResults.LastKMeansResult = completed;
                        m_SelectedModelCache.Clear();
                    });
        }
        else
        {
            m_ClusteringService = nullptr;
        }
        m_PointCloudConsolidationService =
            services.Find<PointCloudConsolidationService>();
        if (m_PointCloudConsolidationService != nullptr &&
            m_PointCloudConsolidationService->Available())
        {
            m_PointCloudConsolidationCompletionSubscription =
                m_PointCloudConsolidationService->SubscribeCompleted(
                    [epoch = m_AttachmentEpoch, this](
                        const PointCloudConsolidationResult& completed)
                    {
                        if (!AttachmentEpochIsActive(epoch))
                            return;
                        m_PointCloudServiceResults.LastPointCloudConsolidationResult = completed;
                        m_SelectedModelCache.Clear();
                    });
        }
        else
        {
            m_PointCloudConsolidationService = nullptr;
        }
    }

    bool EditorWorkspaceSession::Impl::PrepareFrame(
        const EditorWorkspaceSnapshotRequest& request,
        std::string pendingAssetImportPath,
        const Assets::AssetPayloadKind pendingAssetImportPayloadKind,
        std::string pendingSceneFilePath)
    {
        m_FramePrepared = false;
        m_Context = {};
        m_ProcessingContext = {};
        m_ResultBindings = {};
        m_PointCloudServices = {};
        m_LastFrame = {};
        if (m_Worlds == nullptr || m_Services == nullptr ||
            !AttachmentEpochIsActive(m_AttachmentEpoch))
        {
            return false;
        }
        const AssetWorkflowModule* const assetWorkflow =
            m_Services->Find<AssetWorkflowModule>();
        const std::optional<RuntimeAssetImportEvent>* const runtimeImport =
            assetWorkflow != nullptr
                ? &assetWorkflow->GetLastAssetImportEvent()
                : nullptr;
        if (runtimeImport != nullptr &&
            runtimeImport->has_value() &&
            (*runtimeImport)->Sequence !=
                m_LastObservedRuntimeImportSequence)
        {
            m_LastImportResult =
                ProjectEditorFileImportResult(**runtimeImport);
            m_LastObservedRuntimeImportSequence =
                (*runtimeImport)->Sequence;
        }
        SceneDocumentModule* const sceneDocuments = m_Services->Find<SceneDocumentModule>();
        const std::optional<RuntimeSceneFileEvent>* runtimeSceneFile =
            sceneDocuments != nullptr
                ? &sceneDocuments->GetLastSceneFileEvent()
                : nullptr;
        if (runtimeSceneFile != nullptr && runtimeSceneFile->has_value() &&
            (*runtimeSceneFile)->Sequence != m_LastObservedRuntimeSceneFileSequence)
        {
            m_LastSceneFileResult = ProjectEditorSceneFileResult(**runtimeSceneFile);
            m_LastObservedRuntimeSceneFileSequence = (*runtimeSceneFile)->Sequence;
        }
        m_Context = MakeEditorFeatureBindings(*m_Worlds, *m_Services);
        EditorFeatureBindings& context = m_Context;
        context.AttachmentActive = [epoch = m_AttachmentEpoch]
        { return AttachmentEpochIsActive(epoch); };
        context.InvalidateWorkspaceSnapshotCache = [epoch = m_AttachmentEpoch, this]
        {
            if (AttachmentEpochIsActive(epoch))
                m_SelectedModelCache.Clear();
        };
        GuardAttachmentCommandSurfaces(context, m_AttachmentEpoch);
        m_ParameterizationUvViewCommands =
            MakeEditorParameterizationUvViewCommandSurface(*m_Services);
        GuardParameterizationUvViewSurface(m_ParameterizationUvViewCommands, m_AttachmentEpoch);
        m_ResultBindings.ParameterizationUvViewCommands = &m_ParameterizationUvViewCommands;
        context.SelectedModelCache = &m_SelectedModelCache;
        // The editor owns domain identity while `JobService` owns lifecycle.
        // Keep the token/identity index bounded, then expose only submit, active
        // output lookup, and per-entity queue projection. Every callback checks
        // the attachment epoch before reaching session-owned state.
        if (m_Jobs != nullptr)
        {
            PruneEditorJobIdentities(m_Jobs->SnapshotAll(), m_JobIdentities);
            context.JobCommands.Submit = [epoch = m_AttachmentEpoch, this](
                                             JobDesc desc, EditorJobIdentity identity) -> JobToken
            {
                if (!AttachmentEpochIsActive(epoch) || m_Jobs == nullptr)
                    return JobToken{};
                desc.Scope = m_Worlds->ActiveWorld();
                const JobToken token = m_Jobs->Submit(std::move(desc));
                if (token.IsValid())
                    m_JobIdentities.insert_or_assign(token, std::move(identity));
                return token;
            };
            context.JobCommands.FindActive =
                [epoch = m_AttachmentEpoch,
                 this](const EditorJobIdentity& identity)
                    -> std::optional<EditorJobRecord>
                {
                    if (!AttachmentEpochIsActive(epoch) || m_Jobs == nullptr)
                        return std::nullopt;
                    return FindActiveEditorJob(
                        *m_Jobs,
                        m_JobIdentities,
                        identity);
                };
            context.JobCommands.SnapshotEntity =
                [epoch = m_AttachmentEpoch,
                 this](const std::uint32_t stableEntityId)
                    -> std::vector<EditorJobRecord>
                {
                    if (!AttachmentEpochIsActive(epoch) || m_Jobs == nullptr)
                        return {};
                    return SnapshotEditorJobsForEntity(
                        *m_Jobs,
                        m_JobIdentities,
                        stableEntityId);
                };
        }
        context.SpatialIndices = m_SpatialIndices;
        m_PointCloudServices.Clustering = m_ClusteringService;
        m_PointCloudServices.PointCloudConsolidation =
            m_PointCloudConsolidationService;
        m_ResultBindings.PointCloudServices = &m_PointCloudServices;
        m_PointFieldResultSinks.DismissResult = [epoch = m_AttachmentEpoch, this](EditorPointFieldResultSlot slot)
        {
            if (!AttachmentEpochIsActive(epoch)) return;
            switch (slot)
            {
            case EditorPointFieldResultSlot::KernelDensity: m_PointFieldResults.LastKernelDensityResult.reset(); break;
            case EditorPointFieldResultSlot::PointSpacing: m_PointFieldResults.LastPointSpacingResult.reset(); break;
            }
        };
        m_PointAnalysisResultSinks.DismissResult = [epoch = m_AttachmentEpoch, this](EditorPointAnalysisResultSlot slot)
        {
            if (!AttachmentEpochIsActive(epoch)) return;
            switch (slot)
            {
            case EditorPointAnalysisResultSlot::OutlierAnalysis: m_PointAnalysisResults.LastOutlierAnalysisResult.reset(); break;
            case EditorPointAnalysisResultSlot::KeypointAnalysis: m_PointAnalysisResults.LastKeypointAnalysisResult.reset(); break;
            case EditorPointAnalysisResultSlot::DensityWeight: m_PointAnalysisResults.LastDensityWeightResult.reset(); break;
            case EditorPointAnalysisResultSlot::DescriptorAnalysis: m_PointAnalysisResults.LastDescriptorAnalysisResult.reset(); break;
            }
        };
        m_PointSetResultSinks.DismissResult = [epoch = m_AttachmentEpoch, this](EditorPointSetResultSlot slot)
        {
            if (!AttachmentEpochIsActive(epoch)) return;
            switch (slot)
            {
            case EditorPointSetResultSlot::BilateralFilter: m_PointSetResults.LastBilateralFilterResult.reset(); break;
            case EditorPointSetResultSlot::ProgressivePoisson: m_PointSetResults.LastProgressivePoissonResult.reset(); break;
            }
        };
        m_PointConstructionResultSinks.DismissResult =
            [epoch = m_AttachmentEpoch, this](EditorPointConstructionResultSlot slot)
        {
            if (!AttachmentEpochIsActive(epoch)) return;
            switch (slot)
            {
            case EditorPointConstructionResultSlot::PointConstruction:
                m_PointConstructionResults.LastPointConstructionResult.reset(); break;
            }
        };
        m_PointCloudServiceResultSinks.DismissResult =
            [epoch = m_AttachmentEpoch, this](EditorPointCloudServiceResultSlot slot)
        {
            if (!AttachmentEpochIsActive(epoch)) return;
            switch (slot)
            {
            case EditorPointCloudServiceResultSlot::KMeans:
                m_PointCloudServiceResults.LastKMeansResult.reset(); break;
            case EditorPointCloudServiceResultSlot::PointCloudConsolidation:
                m_PointCloudServiceResults.LastPointCloudConsolidationResult.reset(); break;
            }
        };
        const auto retain = [epoch = m_AttachmentEpoch]<typename Storage, typename Result>(
            Storage& storage, std::optional<Result> Storage::* member)
        {
            return [epoch, storage = &storage, member](Result result)
            {
                if (AttachmentEpochIsActive(epoch))
                    storage->*member = std::move(result);
            };
        };
        m_PointSetResultSinks.ProgressivePoisson =
            retain(m_PointSetResults, &EditorPointSetResultsSnapshot::LastProgressivePoissonResult);
        m_ParameterizationResultSinks.UvRegeneration = retain(
            m_ParameterizationResults,
            &EditorParameterizationResultsSnapshot::LastUvRegenerationResult);
        m_ParameterizationResultSinks.Parameterization = retain(
            m_ParameterizationResults,
            &EditorParameterizationResultsSnapshot::LastParameterizationResult);
        m_ParameterizationResultSinks.DismissResult = [epoch = m_AttachmentEpoch, this]
        {
            if (epoch && epoch->load()) m_ParameterizationResults.LastParameterizationResult.reset();
        };
        m_ParameterizationResultSinks.DismissUvRegenerationResult =
            [epoch = m_AttachmentEpoch, this]
        {
            if (epoch && epoch->load()) m_ParameterizationResults.LastUvRegenerationResult.reset();
        };
        m_MeshFieldResultSinks.MeshCurvature =
            retain(m_MeshFieldResults, &EditorMeshFieldResultsSnapshot::LastMeshCurvatureResult);
        m_MeshFieldResultSinks.DismissResult = [epoch = m_AttachmentEpoch, this]
        {
            if (epoch && epoch->load()) m_MeshFieldResults.LastMeshCurvatureResult.reset();
        };
        m_MeshTopologyResultSinks.MeshDenoise =
            retain(m_MeshTopologyResults, &EditorMeshTopologyResultsSnapshot::LastMeshDenoiseResult);
        m_MeshTopologyResultSinks.MeshRemesh =
            retain(m_MeshTopologyResults, &EditorMeshTopologyResultsSnapshot::LastMeshRemeshResult);
        m_MeshTopologyResultSinks.MeshSubdivide =
            retain(m_MeshTopologyResults, &EditorMeshTopologyResultsSnapshot::LastMeshSubdivideResult);
        m_MeshTopologyResultSinks.MeshSimplify =
            retain(m_MeshTopologyResults, &EditorMeshTopologyResultsSnapshot::LastMeshSimplifyResult);
        m_MeshTopologyResultSinks.DismissResult =
            [epoch = m_AttachmentEpoch, this](const EditorMeshTopologyResultSlot slot)
        {
            if (!AttachmentEpochIsActive(epoch)) return;
            switch (slot)
            {
            case EditorMeshTopologyResultSlot::MeshDenoise:
                m_MeshTopologyResults.LastMeshDenoiseResult.reset(); break;
            case EditorMeshTopologyResultSlot::MeshRemesh:
                m_MeshTopologyResults.LastMeshRemeshResult.reset(); break;
            case EditorMeshTopologyResultSlot::MeshSubdivide:
                m_MeshTopologyResults.LastMeshSubdivideResult.reset(); break;
            case EditorMeshTopologyResultSlot::MeshSimplify:
                m_MeshTopologyResults.LastMeshSimplifyResult.reset(); break;
            }
        };
        m_RegistrationResultSinks.Registration =
            retain(m_RegistrationResults, &EditorRegistrationResultsSnapshot::LastRegistrationResult);
        m_RegistrationResultSinks.DismissResult = [epoch = m_AttachmentEpoch, this]
        {
            if (epoch && epoch->load()) m_RegistrationResults.LastRegistrationResult.reset();
        };
        m_NormalResultSinks.NormalEstimation =
            retain(m_NormalResults, &EditorNormalResultsSnapshot::LastNormalEstimationResult);
        m_NormalResultSinks.DismissResult = [epoch = m_AttachmentEpoch, this]
        {
            if (epoch && epoch->load()) m_NormalResults.LastNormalEstimationResult.reset();
        };
        m_PointAnalysisResultSinks.OutlierAnalysis =
            retain(m_PointAnalysisResults, &EditorPointAnalysisResultsSnapshot::LastOutlierAnalysisResult);
        m_PointFieldResultSinks.KernelDensity =
            retain(m_PointFieldResults, &EditorPointFieldResultsSnapshot::LastKernelDensityResult);
        m_PointFieldResultSinks.PointSpacing =
            retain(m_PointFieldResults, &EditorPointFieldResultsSnapshot::LastPointSpacingResult);
        m_PointSetResultSinks.BilateralFilter =
            retain(m_PointSetResults, &EditorPointSetResultsSnapshot::LastBilateralFilterResult);
        m_PointAnalysisResultSinks.KeypointAnalysis =
            retain(m_PointAnalysisResults, &EditorPointAnalysisResultsSnapshot::LastKeypointAnalysisResult);
        m_PointAnalysisResultSinks.DescriptorAnalysis =
            retain(m_PointAnalysisResults, &EditorPointAnalysisResultsSnapshot::LastDescriptorAnalysisResult);
        m_PointAnalysisResultSinks.DensityWeight =
            retain(m_PointAnalysisResults, &EditorPointAnalysisResultsSnapshot::LastDensityWeightResult);
        m_PointConstructionResultSinks.PointConstruction =
            retain(m_PointConstructionResults, &EditorPointConstructionResultsSnapshot::LastPointConstructionResult);
        context.PendingAssetImportPath =
            std::move(pendingAssetImportPath);
        context.PendingAssetImportPayloadKind =
            pendingAssetImportPayloadKind;
        context.PendingSceneFilePath =
            std::move(pendingSceneFilePath);
        if (m_LastSceneFileResult.has_value())
            context.LastSceneFileResult = &*m_LastSceneFileResult;
        if (m_LastImportResult.has_value())
            context.LastAssetImportResult = &*m_LastImportResult;
        m_ResultBindings.PointFieldResults = &m_PointFieldResults;
        m_ResultBindings.PointAnalysisResults = &m_PointAnalysisResults;
        m_ResultBindings.NormalResults = &m_NormalResults;
        m_ResultBindings.RegistrationResults = &m_RegistrationResults;
        m_ResultBindings.MeshFieldResults = &m_MeshFieldResults;
        m_ResultBindings.MeshTopologyResults = &m_MeshTopologyResults;
        m_ResultBindings.ParameterizationResults = &m_ParameterizationResults;
        m_ResultBindings.PointSetResults = &m_PointSetResults;
        m_ResultBindings.PointConstructionResults = &m_PointConstructionResults;
        m_ResultBindings.PointCloudServiceResults = &m_PointCloudServiceResults;
        m_ResultBindings.PointFieldResultSinks = &m_PointFieldResultSinks;
        m_ResultBindings.PointAnalysisResultSinks = &m_PointAnalysisResultSinks;
        m_ResultBindings.NormalResultSinks = &m_NormalResultSinks;
        m_ResultBindings.RegistrationResultSinks = &m_RegistrationResultSinks;
        m_ResultBindings.MeshFieldResultSinks = &m_MeshFieldResultSinks;
        m_ResultBindings.MeshTopologyResultSinks = &m_MeshTopologyResultSinks;
        m_ResultBindings.ParameterizationResultSinks = &m_ParameterizationResultSinks;
        m_ResultBindings.PointSetResultSinks = &m_PointSetResultSinks;
        m_ResultBindings.PointConstructionResultSinks = &m_PointConstructionResultSinks;
        m_ResultBindings.PointCloudServiceResultSinks = &m_PointCloudServiceResultSinks;
        const Core::Extent2D viewport =
            context.CameraViewport.Width != 0u &&
                    context.CameraViewport.Height != 0u
                ? context.CameraViewport
                : Core::Extent2D{.Width = 1280u, .Height = 720u};
        const Graphics::RenderFrameInput recipeInput{
            .Viewport = viewport,
            .Camera = Graphics::CameraViewInput{.Valid = true},
        };
        m_RenderRecipeContext = Graphics::RenderRecipeConfigContext{
            .Renderer = Graphics::MakeCurrentRendererDescriptor(),
            .BaseRecipe = Graphics::MakeCurrentRendererRecipeDescriptor(),
            .BaseViewOutput =
                Graphics::MakeCurrentRendererViewOutputRecipe(recipeInput),
            .BaseBindings = Graphics::MakeCurrentRendererBindingSet(),
        };
        context.RenderRecipeContext = &m_RenderRecipeContext;
        context.RenderRecipeEditorState = &m_RenderRecipeState;
        context.RenderArtifacts = &m_RenderArtifactRegistry;
        EngineConfigControl* configControl = m_Services->Find<EngineConfigControl>();
        context.RenderRecipeCommandsAvailable =
            configControl != nullptr &&
            context.PreviewRenderRecipeDocument &&
            context.ApplyRenderRecipePreview;
        if (configControl != nullptr)
        {
            context.EngineConfigControlState =
                &configControl->GetEngineConfigControlState();
            context.PreviewEngineConfigDocument =
                [epoch = m_AttachmentEpoch,
                 configControl](const std::string& document,
                                const std::string& sourceId)
                {
                    if (!AttachmentEpochIsActive(epoch))
                        return Core::Config::EngineConfigLoadResult{};
                    return configControl
                        ->PreviewEngineConfigControlDocument(
                            document,
                            sourceId);
                };
            context.ApplyEngineConfigHotSubset =
                [epoch = m_AttachmentEpoch,
                 configControl](
                    const Core::Config::EngineConfigLoadResult&
                        loadResult)
                {
                    if (!AttachmentEpochIsActive(epoch))
                    {
                        return RuntimeEngineConfigApplyResult{
                            .Status =
                                RuntimeEngineConfigApplyStatus::
                                    Rejected,
                            .Source =
                                RuntimeConfigControlSource::Editor,
                        };
                    }
                    return configControl->ApplyEngineConfigHotSubset(
                        loadResult,
                        RuntimeConfigControlSource::Editor);
                };
            context.EngineConfigCommandsAvailable = true;
        }
        // Capture only after epoch guards and config/job callbacks are installed.
        m_ProcessingContext = MakeEditorProcessingContext(context);
        m_ProcessingContext.PointInputReadiness = m_PointInputReadiness;
        BeginEditorPointInputReadinessFrame(*m_PointInputReadiness);
        m_LastFrame = BuildEditorWorkspaceSnapshot(
            MakeEditorWorkspaceSnapshotContext(context, m_ProcessingContext),
            request);
        context.ModelBuildStats = &m_LastFrame.ModelBuildStats;
        m_FramePrepared = true;
        return true;
    }

    bool EditorWorkspaceSession::Impl::VisitPreparedFrame(
        const EditorWorkspacePreparedFrameVisitor& visitor)
    {
        if (!m_FramePrepared || !visitor)
            return false;

        visitor(EditorWorkspacePreparedFrame{
            .Context = m_Context,
            .Frame = m_LastFrame,
            .Geometry = m_ProcessingContext,
            .Results = m_ResultBindings,
        });
        return true;
    }

    void EditorWorkspaceSession::Impl::Detach()
    {
        if (m_AttachmentEpoch != nullptr)
        {
            m_AttachmentEpoch->store(false, std::memory_order_release);
        }
        if (m_Worlds != nullptr && m_Services != nullptr)
        {
            if (m_ClusteringService != nullptr &&
                m_KMeansCompletionSubscription.IsValid())
            {
                m_ClusteringService->Unsubscribe(
                    m_KMeansCompletionSubscription);
            }
            m_KMeansCompletionSubscription = {};
            m_ClusteringService = nullptr;
            if (m_PointCloudConsolidationService != nullptr &&
                m_PointCloudConsolidationCompletionSubscription.IsValid())
            {
                m_PointCloudConsolidationService->Unsubscribe(
                    m_PointCloudConsolidationCompletionSubscription);
            }
            m_PointCloudConsolidationCompletionSubscription = {};
            m_PointCloudConsolidationService = nullptr;
            m_Jobs = nullptr;
            m_Worlds   = nullptr;
            m_Services = nullptr;
        }
        else
        {
            m_KMeansCompletionSubscription = {};
            m_ClusteringService = nullptr;
            m_PointCloudConsolidationCompletionSubscription = {};
            m_PointCloudConsolidationService = nullptr;
            m_Jobs = nullptr;
        }
        m_AttachmentEpoch.reset();
        ResetAttachmentState();
    }

    void EditorWorkspaceSession::Impl::ResetAttachmentState()
    {
        m_FramePrepared = false;
        m_Context = {};
        // Clear borrowed references before resetting their session-owned storage.
        m_ProcessingContext = {};
        m_ResultBindings = {};
        m_PointCloudServices = {};
        m_LastFrame = {};
        m_SelectedModelCache = {};
        m_PointInputReadiness.reset();
        m_LastObservedRuntimeImportSequence = 0u;
        m_LastObservedRuntimeSceneFileSequence = 0u;
        m_LastImportResult.reset();
        m_LastSceneFileResult.reset();
        m_PointFieldResults = {};
        m_PointAnalysisResults = {};
        m_NormalResults = {};
        m_RegistrationResults = {};
        m_MeshFieldResults = {};
        m_MeshTopologyResults = {};
        m_ParameterizationResults = {};
        m_PointSetResults = {};
        m_PointConstructionResults = {};
        m_PointCloudServiceResults = {};
        m_PointFieldResultSinks = {};
        m_PointAnalysisResultSinks = {};
        m_NormalResultSinks = {};
        m_RegistrationResultSinks = {};
        m_MeshFieldResultSinks = {};
        m_MeshTopologyResultSinks = {};
        m_ParameterizationResultSinks = {};
        m_PointSetResultSinks = {};
        m_PointConstructionResultSinks = {};
        m_PointCloudServiceResultSinks = {};
        m_ParameterizationUvViewCommands = {};
        m_JobIdentities.clear();
        m_RenderRecipeContext = {};
        m_RenderRecipeState = {};
        m_RenderArtifactRegistry = {};
    }


    EditorWorkspaceSession::EditorWorkspaceSession() : m_Impl(std::make_unique<Impl>()) {}
    EditorWorkspaceSession::~EditorWorkspaceSession() = default;
    void EditorWorkspaceSession::Attach(WorldRegistry& worlds, ServiceRegistry& services)
    {
        m_Impl->Attach(worlds, services);
    }
    void EditorWorkspaceSession::Detach() { m_Impl->Detach(); }
    bool EditorWorkspaceSession::PrepareFrame(
        const EditorWorkspaceSnapshotRequest& request, std::string pendingAssetImportPath,
        Assets::AssetPayloadKind pendingAssetImportPayloadKind, std::string pendingSceneFilePath)
    {
        return m_Impl->PrepareFrame(request, std::move(pendingAssetImportPath),
                                   pendingAssetImportPayloadKind, std::move(pendingSceneFilePath));
    }
    bool EditorWorkspaceSession::VisitPreparedFrame(const EditorWorkspacePreparedFrameVisitor& visitor)
    {
        return m_Impl->VisitPreparedFrame(visitor);
    }
    bool EditorWorkspaceSession::IsAttached() const noexcept { return m_Impl->IsAttached(); }
} // namespace Extrinsic::Runtime::EditorFeatureDetail
