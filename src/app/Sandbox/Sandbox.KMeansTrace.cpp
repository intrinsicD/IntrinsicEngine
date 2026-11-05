module;

#include <array>
#include <chrono>
#include <string>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

module Extrinsic.Sandbox.KMeansTrace;

import Extrinsic.Runtime.ClusteringTypes;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.RenderExtraction;

namespace Extrinsic::Sandbox
{
    namespace
    {
        using Pacing = Runtime::RuntimeFramePacingDiagnostics;
        using Stats = Runtime::ClusteringModuleStats;

        template <typename T>
        struct Field
        {
            std::string_view Name;
            std::uint64_t T::*Member;
        };

        // Every interval of the pacing record; nested intervals (the drains inside maintenance) are kept as such.
        constexpr std::array<Field<Pacing>, 31> kPacing{{
            {"total_us", &Pacing::TotalMicros},
            {"platform_begin_us", &Pacing::PlatformBeginMicros},
            {"resize_us", &Pacing::ResizeMicros},
            {"operational_transition_us", &Pacing::OperationalTransitionMicros},
            {"fixed_step_us", &Pacing::FixedStepMicros},
            {"imgui_begin_us", &Pacing::ImGuiBeginMicros},
            {"variable_tick_us", &Pacing::VariableTickMicros},
            {"imgui_end_us", &Pacing::ImGuiEndMicros},
            {"imgui_editor_callback_us", &Pacing::ImGuiEditorCallbackMicros},
            {"imgui_draw_data_copy_us", &Pacing::ImGuiDrawDataCopyMicros},
            {"pre_render_setup_us", &Pacing::PreRenderSetupMicros},
            {"pre_render_transform_flush_us", &Pacing::PreRenderTransformFlushMicros},
            {"selection_pick_drain_us", &Pacing::SelectionPickDrainMicros},
            {"render_contract_us", &Pacing::RenderContractMicros},
            {"render_begin_frame_us", &Pacing::RenderBeginFrameMicros},
            {"render_extraction_us", &Pacing::RenderExtractionMicros},
            {"render_prepare_us", &Pacing::RenderPrepareMicros},
            {"render_execute_us", &Pacing::RenderExecuteMicros},
            {"render_end_frame_us", &Pacing::RenderEndFrameMicros},
            {"render_graph_compile_us", &Pacing::RenderGraphCompileMicros},
            {"render_graph_execute_us", &Pacing::RenderGraphExecuteMicros},
            {"present_us", &Pacing::PresentMicros},
            {"maintenance_us", &Pacing::MaintenanceMicros},
            {"command_drain_us", &Pacing::CommandDrainMicros},
            {"completion_drain_us", &Pacing::JobCompletionDrainMicros},
            {"transfer_collect_us", &Pacing::TransferCollectMicros},
            {"gpu_transfer_drain_us", &Pacing::GpuTransferDrainMicros},
            {"gpu_queue_record_us", &Pacing::GpuQueueRecordMicros},
            {"selection_readback_us", &Pacing::SelectionReadbackMicros},
            {"release_render_world_us", &Pacing::ReleaseRenderWorldMicros},
            {"frame_index", &Pacing::FrameIndex},
        }};

        constexpr std::array<Field<Stats>, 26> kClustering{{
            {"requests_submitted", &Stats::RequestsSubmitted},
            {"last_request_steady_us", &Stats::LastRequestSteadyMicros},
            {"commands_handled", &Stats::CommandsHandled},
            {"jobs_submitted", &Stats::JobsSubmitted},
            {"gpu_requests_accepted", &Stats::GpuRequestsAccepted},
            {"gpu_fallbacks", &Stats::GpuFallbacks},
            {"gpu_completions", &Stats::GpuCompletions},
            {"completion_events", &Stats::CompletionEvents},
            {"labels_committed", &Stats::LabelsCommitted},
            {"commits_dropped", &Stats::CommitsDropped},
            {"capture_reuses", &Stats::CaptureReuses},
            {"cached_capture_points", &Stats::CachedCapturePoints},
            {"capture_init_us", &Stats::GpuCaptureInitMicros},
            {"snapshot_us", &Stats::GpuSnapshotMicros},
            {"input_resolve_us", &Stats::GpuInputResolveMicros},
            {"seeding_us", &Stats::GpuSeedingMicros},
            {"workspace_prepare_us", &Stats::GpuWorkspacePrepareMicros},
            {"advance_us", &Stats::GpuAdvanceMicros},
            {"preview_publish_us", &Stats::GpuPreviewPublishMicros},
            {"accept_publish_us", &Stats::GpuAcceptPublishMicros},
            {"replacements_held", &Stats::GpuReplacementsHeld},
            {"replacements_superseded", &Stats::GpuReplacementsSuperseded},
            {"replacements_started", &Stats::GpuReplacementsStarted},
            {"iterations", &Stats::GpuIterations},
            {"previews", &Stats::GpuPreviews},
            {"submissions", &Stats::GpuSubmissions},
        }};

        template <typename T, std::size_t N>
        nlohmann::ordered_json ToJson(const T& value, const std::array<Field<T>, N>& fields)
        {
            nlohmann::ordered_json out = nlohmann::ordered_json::object();
            for (const auto& field : fields)
                out[std::string(field.Name)] = value.*(field.Member);
            return out;
        }
    }

    KMeansTraceModule::KMeansTraceModule(std::vector<KMeansTraceSample>& samples, const std::uint32_t targetFrames,
        std::function<Runtime::RuntimeFramePacingDiagnostics()> readLastPacing, std::function<void()> requestExit,
        const std::uint32_t exitAfterRuns)
        : m_Samples(samples)
        , m_TargetFrames(targetFrames)
        , m_ExitAfterRuns(exitAfterRuns)
        , m_ReadLastPacing(std::move(readLastPacing))
        , m_RequestExit(std::move(requestExit))
    {
        m_Samples.reserve(std::size_t(targetFrames) + 8u); // no reallocation in the frame loop up to the target
    }

    Runtime::RuntimeModuleResult KMeansTraceModule::OnRegister(Runtime::EngineSetup& setup)
    {
        return setup.RegisterFrameHook(Runtime::FramePhase::Maintenance,
            // The pacing record's index names the frame; the hook context's FrameIndex has already moved past it
            // by Maintenance (it counts render extractions).
            [this](Runtime::RuntimeFrameHookContext& context) { Sample(context.Pacing.FrameIndex); });
    }

    Runtime::RuntimeModuleResult KMeansTraceModule::OnResolve(Runtime::EngineSetup& setup)
    {
        m_Clustering = setup.Services().Find<Runtime::ClusteringService>();
        m_Extraction = setup.Services().Find<Runtime::RenderExtractionCache>();
        return Runtime::RuntimeModuleOk();
    }

    void KMeansTraceModule::OnShutdown(Runtime::RuntimeModuleShutdownContext&)
    {
        m_Clustering = nullptr;
        m_Extraction = nullptr;
    }

    void KMeansTraceModule::AttachCompletedPacing()
    {
        if (!m_ReadLastPacing)
            return;
        const auto pacing = m_ReadLastPacing();
        if (!pacing.Valid)
            return;
        // The completed frame is the newest sample's or, during a frame's Maintenance, the one before it.
        for (auto it = m_Samples.rbegin(); it != m_Samples.rend() && it - m_Samples.rbegin() < 2; ++it)
            if (it->FrameIndex == pacing.FrameIndex && !it->Pacing)
                it->Pacing = pacing;
    }

    void KMeansTraceModule::Sample(const std::uint64_t frameIndex)
    {
        AttachCompletedPacing();
        m_Samples.push_back({
            .FrameIndex = frameIndex,
            .SteadyMicros = std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()),
            .Clustering = m_Clustering != nullptr ? m_Clustering->Stats() : Runtime::ClusteringModuleStats{},
            .ScalarGpuFrontsObserved =
                m_Extraction != nullptr ? m_Extraction->GetLastStats().VisualizationRecipeScalarGpuFrontsObserved : 0u,
        });
        const bool runsDone = m_ExitAfterRuns != 0u && m_Samples.back().Clustering.LabelsCommitted >= m_ExitAfterRuns &&
            ++m_TailFrames >= kTailFrames;
        if ((runsDone || (m_TargetFrames != 0u && m_Samples.size() >= m_TargetFrames)) && m_RequestExit)
            m_RequestExit();
    }

    bool WriteKMeansTrace(const std::filesystem::path& path, const std::span<const KMeansTraceSample> samples)
    {
        nlohmann::ordered_json frames = nlohmann::ordered_json::array();
        for (const auto& sample : samples)
            frames.push_back({{"frame_index", sample.FrameIndex}, {"steady_us", sample.SteadyMicros},
                {"scalar_gpu_fronts_observed", sample.ScalarGpuFrontsObserved},
                {"clustering", ToJson(sample.Clustering, kClustering)},
                {"pacing", sample.Pacing ? ToJson(*sample.Pacing, kPacing) : nlohmann::ordered_json(nullptr)}});
        const nlohmann::ordered_json trace{{"schema", "arch022.kmeans_trace.v1"}, {"source", "ExtrinsicSandbox"},
            {"frame_count", samples.size()}, {"frames", std::move(frames)}};
        if (const auto parent = path.parent_path(); !parent.empty())
        {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
        }
        std::ofstream out(path);
        out << trace.dump() << '\n';
        return bool(out);
    }
}
