module;
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
module Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EngineConfigControl;
// Named only so the shared job declarations in the point-field header resolve.
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Geometry.Properties;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        bool CanApplyProcessingConfig(const EditorProcessingContext& context) noexcept
        {
            return context.EngineConfigControlState != nullptr &&
                   context.EngineConfigCommandsAvailable &&
                   static_cast<bool>(context.PreviewEngineConfigDocument) &&
                   static_cast<bool>(context.ApplyEngineConfigHotSubset);
        }
    }

    bool EditorProcessingCommands::IsBound() const noexcept
    {
        // Prepared command handles borrow a world's scene on the main thread.
        // Their session state can reject a switched/destroyed world before use.
        return m_Context && (!m_Context->AttachmentActive || m_Context->AttachmentActive()) &&
               GeometryProcessingDetail::EditorProcessingContextWorldCurrent(*m_Context);
    }
    extern "C++" const EditorProcessingContext& EditorProcessingCommandsAccess::Resolve(const EditorProcessingCommands& commands) noexcept
    {
        static const EditorProcessingContext empty{};
        return commands.IsBound() ? *commands.m_Context : empty;
    }
    EditorProcessingCommands BindEditorProcessingCommands(EditorProcessingContext context)
    {
        EditorProcessingCommands commands;
        commands.m_Context = std::make_shared<const EditorProcessingContext>(std::move(context));
        return commands;
    }
    EditorOperationProgress GetEditorOperationProgress(
        const EditorProcessingCommands& commands, const EditorOperationRunKey& key)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        return context.JobCommands.Progress ? context.JobCommands.Progress(key)
                                            : EditorOperationProgress{};
    }
    std::uint64_t GetEditorSceneEpoch(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        return context.JobCommands.SceneEpoch ? context.JobCommands.SceneEpoch() : 0u;
    }
    std::vector<EditorJobRecord> GetEditorJobs(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        return context.JobCommands.SnapshotAll ? context.JobCommands.SnapshotAll() : std::vector<EditorJobRecord>{};
    }
    EditorJobCancelStatus CancelEditorJob(const EditorProcessingCommands& commands, const JobToken token)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        return context.JobCommands.Cancel ? context.JobCommands.Cancel(token) : EditorJobCancelStatus::Unavailable;
    }
    JobServiceStats GetEditorJobStats(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        return context.JobCommands.Stats ? context.JobCommands.Stats() : JobServiceStats{};
    }
    ActionReadiness ResolveEditorJobCancelReadiness(const EditorProcessingCommands& commands, const EditorJobRecord& job)
    {
        const auto& surface = EditorProcessingCommandsAccess::Resolve(commands).JobCommands;
        if (!IsActiveEditorJobState(job.State))
            return {false, "The job already ended."};
        const bool serviceRun = job.CorrelationId != 0u && job.Identity.EntityId == 0u && job.Identity.OutputName.empty();
        if (serviceRun)
            return {false, "Not an editor job: K-Means and consolidation runs are stopped from their own panels."};
        if (!surface.Cancel)
            return {false, "Job cancel is unavailable. Open an active editor session."};
        const JobToken run = job.Identity.Run.IsValid() ? job.Identity.Run : job.Token;
        if (!job.Identity.Auxiliary && surface.RunCancelRequested && surface.RunCancelRequested(run))
            return {false, "Cancel already requested; waiting for the job to stop."};
        return {true, {}};
    }
    EditorJobCancelStatus CancelEditorJobRun(const EditorProcessingCommands& commands, const EditorJobRecord& job)
    {
        if (job.Identity.Auxiliary)
            return CancelEditorJob(commands, job.Token);
        const JobToken run = job.Identity.Run.IsValid() ? job.Identity.Run : job.Token;
        const EditorRunCancelCount count = CancelEditorRunJobs(commands, std::span<const JobToken>{&run, 1u});
        if (count.Unavailable) return EditorJobCancelStatus::Unavailable;
        return count.Requested > 0u ? EditorJobCancelStatus::Requested : EditorJobCancelStatus::NotActive;
    }
    EditorRunCancelCount CancelEditorRunJobs(const EditorProcessingCommands& commands, const std::span<const JobToken> runs)
    {
        return CancelEditorRuns(EditorProcessingCommandsAccess::Resolve(commands).JobCommands, runs);
    }
    bool IsEditorRunCancelRequested(const EditorProcessingCommands& commands, const JobToken run)
    {
        const auto& surface = EditorProcessingCommandsAccess::Resolve(commands).JobCommands;
        return surface.RunCancelRequested && surface.RunCancelRequested(run);
    }
    bool AreEditorProcessingConfigCommandsAvailable(
        const EditorProcessingCommands& commands) noexcept
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        return CanApplyProcessingConfig(context);
    }
    std::string_view ToString(const ActionReadinessCode code) noexcept
    {
        using C = ActionReadinessCode;
        switch (code)
        {
        case C::Ok: return "ok";
        case C::WorkspaceUnavailable: return "workspace_unavailable";
        case C::MissingEntity: return "missing_entity";
        case C::WrongDomain: return "wrong_domain";
        case C::MissingProperty: return "missing_property";
        case C::IncompatibleProperty: return "incompatible_property";
        case C::ElementCountMismatch: return "element_count_mismatch";
        case C::InvalidConfig: return "invalid_config";
        case C::ConflictingOptions: return "conflicting_options";
        case C::DeviceUnavailable: return "device_unavailable";
        case C::KernelUnavailable: return "kernel_unavailable";
        case C::JobActive: return "job_active";
        case C::StaleInput: return "stale_input";
        case C::PendingVerdict: return "pending_verdict";
        case C::Unclassified: return "unclassified";
        }
        return "unclassified";
    }
    ActionReadiness MakeActionReadiness(std::vector<ActionReadinessReason> reasons)
    {
        ActionReadiness readiness{.Enabled = reasons.empty()};
        if (!reasons.empty()) readiness.DisabledReason = reasons.front().Message;
        readiness.Reasons = std::move(reasons);
        return readiness;
    }
    std::vector<ActionReadinessReason> ActionReadinessReasons(const ActionReadiness& readiness)
    {
        if (readiness.Enabled) return {};
        if (!readiness.Reasons.empty()) return readiness.Reasons;
        return {{.Code = ActionReadinessCode::Unclassified, .Field = {}, .Message = readiness.DisabledReason}};
    }
    ActionReadiness ResolveEditorProcessingActionReadiness(
        const EditorProcessingCommands& commands, ActionReadiness method)
    {
        if (method.Enabled)
            method = MakeActionReadiness({});
        else if (method.DisabledReason.empty() && method.Reasons.empty())
            method = MakeActionReadiness({{.Code = ActionReadinessCode::Unclassified, .Field = {},
                .Message = "Processing prerequisites are unavailable. Check the selected inputs and settings."}});
        if (AreEditorProcessingConfigCommandsAvailable(commands))
            return method;
        // The config lane is missing: lead with it, then keep every reason the method reported.
        std::vector<ActionReadinessReason> reasons{{.Code = ActionReadinessCode::WorkspaceUnavailable, .Field = {},
            .Message = "Processing controls are unavailable. Open an active editor session."}};
        for (auto& reason : ActionReadinessReasons(method)) reasons.push_back(std::move(reason));
        return MakeActionReadiness(std::move(reasons));
    }
    void CarryEditorLabelPrefix(JobDesc& desc, EditorCommandHistory* history, std::string prefix,
                                std::function<bool()> active)
    {
        if (history == nullptr || prefix.empty() || !desc.PublishCompletion) return;
        desc.PublishCompletion = [inner = std::move(desc.PublishCompletion), history, prefix = std::move(prefix),
                                  active = std::move(active)](KernelEventBus& bus, const JobResultEnvelope& result) mutable
        {
            if (active && !active()) return inner(bus, result);
            const ScopedEditorCommandLabelPrefix scope{history, prefix};
            return inner(bus, result);
        };
    }
    GeometryPropertyCatalogSnapshot GetEditorPointInputCatalog(
        const EditorProcessingCommands& commands, std::uint32_t stableId)
    {
        return GeometryProcessingDetail::BuildPointInputCatalog(
            EditorProcessingCommandsAccess::Resolve(commands), stableId);
    }
    EditorPointInputReadinessStats GetEditorPointInputReadinessStats(const EditorProcessingCommands& commands)
    {
        return GeometryProcessingDetail::PointInputReadinessStats(
            EditorProcessingCommandsAccess::Resolve(commands));
    }
    extern "C++" RuntimeEngineConfigApplyResult ApplyEditorProcessingConfig(
        const EditorProcessingCommands& commands,
        const Core::Config::EngineConfigSectionValidationResult& validation,
        const std::string& sourceId, const std::function<void(Core::Config::EngineConfig&)>& update)
    {
        RuntimeEngineConfigApplyResult result{
            .Status = RuntimeEngineConfigApplyStatus::Rejected,
            .Source = RuntimeConfigControlSource::Editor,
        };
        if (!validation.Usable())
        {
            result.LoadResult.Diagnostics = validation.Diagnostics;
            return result;
        }
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!CanApplyProcessingConfig(context))
            return result;
        auto candidate = context.EngineConfigControlState->ActiveConfig;
        update(candidate);
        result.LoadResult = context.PreviewEngineConfigDocument(
            Core::Config::SerializeEngineConfig(candidate), sourceId);
        if (!Core::Config::IsConfigUsable(result.LoadResult)) return result;
        if (result.LoadResult.State == Core::Config::EngineConfigState::FallbackApplied)
        {
            // Reapply the pure section update to detect edits lost to file-load
            // fallback, while tolerating fallback in unrelated sections.
            auto accepted = result.LoadResult.Preview.Config;
            update(accepted);
            if (accepted.AppSections != result.LoadResult.Preview.Config.AppSections)
                return result;
        }
        return context.ApplyEngineConfigHotSubset(result.LoadResult);
    }
}
