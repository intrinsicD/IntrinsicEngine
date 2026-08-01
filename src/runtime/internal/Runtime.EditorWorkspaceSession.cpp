module;

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

module Extrinsic.Runtime.Private.EditorFeatures;

import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime::EditorFeatureDetail
{
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
            context.ParameterizationUvViewCommands.Submit = GuardAttachmentCommand(
                std::move(context.ParameterizationUvViewCommands.Submit), epoch,
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

    EditorWorkspaceSession::EditorWorkspaceSession()
    {
    }

    EditorWorkspaceSession::~EditorWorkspaceSession()
    {
        Detach();
    }

    void EditorWorkspaceSession::Attach(WorldRegistry& worlds, ServiceRegistry& services)
    {
        Detach();
        m_Worlds          = &worlds;
        m_Services        = &services;
        m_AttachmentEpoch = std::make_shared<std::atomic_bool>(true);
        m_Jobs = services.Find<JobService>();
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
                        m_LastKMeansResult = completed;
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
                        m_LastPointCloudConsolidationResult = completed;
                        m_SelectedModelCache.Clear();
                    });
        }
        else
        {
            m_PointCloudConsolidationService = nullptr;
        }
    }

    bool EditorWorkspaceSession::PrepareFrame(
        const EditorWorkspaceSnapshotRequest& request,
        std::string pendingAssetImportPath,
        const EditorAssetPayloadKind pendingAssetImportPayloadKind,
        std::string pendingSceneFilePath)
    {
        m_FramePrepared = false;
        m_Context = {};
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
        context.Clustering = m_ClusteringService;
        context.PointCloudConsolidation =
            m_PointCloudConsolidationService;
        context.MethodResultSinks.ProgressivePoisson =
            [epoch = m_AttachmentEpoch, this](
                EditorProgressivePoissonResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastProgressivePoissonResult =
                        std::move(result);
            };
        context.MethodResultSinks.UvRegeneration =
            [epoch = m_AttachmentEpoch, this](
                EditorUvRegenerationCommandResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastUvRegenerationResult = std::move(result);
            };
        context.MethodResultSinks.Parameterization =
            [epoch = m_AttachmentEpoch, this](
                EditorParameterizationResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastParameterizationResult = std::move(result);
            };
        context.MethodResultSinks.MeshCurvature =
            [epoch = m_AttachmentEpoch, this](
                EditorMeshCurvatureResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastMeshCurvatureResult = std::move(result);
            };
        context.MethodResultSinks.MeshDenoise =
            [epoch = m_AttachmentEpoch, this](
                EditorMeshDenoiseResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastMeshDenoiseResult = std::move(result);
            };
        context.MethodResultSinks.MeshRemesh =
            [epoch = m_AttachmentEpoch, this](
                EditorMeshRemeshResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastMeshRemeshResult = std::move(result);
            };
        context.MethodResultSinks.MeshSubdivide =
            [epoch = m_AttachmentEpoch, this](
                EditorMeshSubdivideResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastMeshSubdivideResult = std::move(result);
            };
        context.MethodResultSinks.MeshSimplify =
            [epoch = m_AttachmentEpoch, this](
                EditorMeshSimplifyResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastMeshSimplifyResult = std::move(result);
            };
        context.MethodResultSinks.MeshVertexNormals =
            [epoch = m_AttachmentEpoch, this](
                EditorMeshVertexNormalsResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastMeshVertexNormalsResult =
                        std::move(result);
            };
        context.MethodResultSinks.GraphVertexNormals =
            [epoch = m_AttachmentEpoch, this](
                EditorGraphVertexNormalsResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastGraphVertexNormalsResult =
                        std::move(result);
            };
        context.MethodResultSinks.PointCloudVertexNormals =
            [epoch = m_AttachmentEpoch, this](
                EditorPointCloudVertexNormalsResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastPointCloudVertexNormalsResult =
                        std::move(result);
            };
        context.MethodResultSinks.PointCloudOutlierRemoval =
            [epoch = m_AttachmentEpoch, this](
                EditorPointCloudOutlierRemovalResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastPointCloudOutlierRemovalResult =
                        std::move(result);
            };
        context.MethodResultSinks.Registration =
            [epoch = m_AttachmentEpoch, this](
                EditorRegistrationResult result)
            {
                if (AttachmentEpochIsActive(epoch))
                    m_LastRegistrationResult = std::move(result);
            };
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
        if (m_LastKMeansResult.has_value())
            context.LastKMeansResult = &*m_LastKMeansResult;
        if (m_LastMeshDenoiseResult.has_value())
            context.LastMeshDenoiseResult =
                &*m_LastMeshDenoiseResult;
        if (m_LastMeshCurvatureResult.has_value())
            context.LastMeshCurvatureResult =
                &*m_LastMeshCurvatureResult;
        if (m_LastMeshRemeshResult.has_value())
            context.LastMeshRemeshResult =
                &*m_LastMeshRemeshResult;
        if (m_LastMeshSubdivideResult.has_value())
            context.LastMeshSubdivideResult =
                &*m_LastMeshSubdivideResult;
        if (m_LastMeshSimplifyResult.has_value())
            context.LastMeshSimplifyResult =
                &*m_LastMeshSimplifyResult;
        if (m_LastMeshVertexNormalsResult.has_value())
            context.LastMeshVertexNormalsResult =
                &*m_LastMeshVertexNormalsResult;
        if (m_LastGraphVertexNormalsResult.has_value())
            context.LastGraphVertexNormalsResult =
                &*m_LastGraphVertexNormalsResult;
        if (m_LastPointCloudVertexNormalsResult.has_value())
            context.LastPointCloudVertexNormalsResult =
                &*m_LastPointCloudVertexNormalsResult;
        if (m_LastPointCloudOutlierRemovalResult.has_value())
            context.LastPointCloudOutlierRemovalResult =
                &*m_LastPointCloudOutlierRemovalResult;
        if (m_LastUvRegenerationResult.has_value())
            context.LastUvRegenerationResult =
                &*m_LastUvRegenerationResult;
        if (m_LastParameterizationResult.has_value())
            context.LastParameterizationResult =
                &*m_LastParameterizationResult;
        if (m_LastPointCloudConsolidationResult.has_value())
            context.LastPointCloudConsolidationResult =
                &*m_LastPointCloudConsolidationResult;
        if (m_LastProgressivePoissonResult.has_value())
            context.LastProgressivePoissonResult =
                &*m_LastProgressivePoissonResult;
        if (m_LastRegistrationResult.has_value())
            context.LastRegistrationResult =
                &*m_LastRegistrationResult;
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
        m_LastFrame = BuildEditorWorkspaceSnapshot(
            EditorWorkspaceSnapshotContext{
                .Scene = MakeEditorSceneEditingContext(context),
                .Geometry = MakeEditorGeometryProcessingContext(context),
                .Visualization =
                    MakeEditorVisualizationEditingContext(context),
                .RenderRecipe =
                    MakeEditorRenderRecipeEditingContext(context),
                .SelectedModelCache = context.SelectedModelCache,
            },
            request);
        context.ModelBuildStats = &m_LastFrame.ModelBuildStats;
        m_FramePrepared = true;
        return true;
    }

    bool EditorWorkspaceSession::VisitPreparedFrame(
        const EditorWorkspacePreparedFrameVisitor& visitor)
    {
        if (!m_FramePrepared || !visitor)
            return false;

        visitor(EditorWorkspacePreparedFrame{
            .Context = m_Context,
            .Frame = m_LastFrame,
            .LastAssetImportResult = m_LastImportResult,
            .LastSceneFileResult = m_LastSceneFileResult,
            .LastUvRegenerationResult = m_LastUvRegenerationResult,
        });
        return true;
    }

    void EditorWorkspaceSession::Detach()
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

    void EditorWorkspaceSession::ResetAttachmentState()
    {
        m_FramePrepared = false;
        m_Context = {};
        m_LastFrame = {};
        m_SelectedModelCache = {};
        m_LastObservedRuntimeImportSequence = 0u;
        m_LastObservedRuntimeSceneFileSequence = 0u;
        m_LastImportResult.reset();
        m_LastSceneFileResult.reset();
        m_LastKMeansResult.reset();
        m_LastPointCloudConsolidationResult.reset();
        m_LastMeshDenoiseResult.reset();
        m_LastMeshCurvatureResult.reset();
        m_LastMeshRemeshResult.reset();
        m_LastMeshSubdivideResult.reset();
        m_LastMeshSimplifyResult.reset();
        m_LastMeshVertexNormalsResult.reset();
        m_LastGraphVertexNormalsResult.reset();
        m_LastPointCloudVertexNormalsResult.reset();
        m_LastPointCloudOutlierRemovalResult.reset();
        m_LastProgressivePoissonResult.reset();
        m_LastUvRegenerationResult.reset();
        m_LastParameterizationResult.reset();
        m_LastRegistrationResult.reset();
        m_JobIdentities.clear();
        m_RenderRecipeContext = {};
        m_RenderRecipeState = {};
        m_RenderArtifactRegistry = {};
    }

} // namespace Extrinsic::Runtime::EditorFeatureDetail
