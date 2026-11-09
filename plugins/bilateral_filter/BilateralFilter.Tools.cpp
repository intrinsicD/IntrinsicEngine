module;

#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

module Intrinsic.Plugin.BilateralFilter;

import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PluginSections;
import Extrinsic.Runtime.StepRuns;

namespace Intrinsic::Plugins::BilateralFilter
{
    namespace
    {
        using Json = nlohmann::json;

        Json ResultJson(const Result& r)
        {
            return {{"status", r.Status},
                {"message", r.Message},
                {"requested_backend", ToString(r.RequestedBackend)},
                {"actual_backend", r.ActualBackend},
                {"output", {{"domain", int(r.Output.Domain)}, {"name", r.Output.Name}}},
                {"slot_count", r.SlotCount},
                {"live_count", r.LiveCount},
                {"written_count", r.WrittenCount},
                {"points_filtered", r.PointsFiltered},
                {"degenerate_normals", r.DegenerateNormals},
                {"average_displacement", r.AverageDisplacement},
                {"max_displacement", r.MaxDisplacement},
                {"spatial_sigma_used", r.SpatialSigmaUsed},
                {"completed_iterations", r.CompletedIterations},
                {"gpu_query_batches", r.GpuQueryBatches},
                {"gpu_query_upload_bytes", r.GpuQueryUploadBytes},
                {"gpu_workspace_upload_bytes", r.GpuWorkspaceUploadBytes},
                {"gpu_readback_bytes", r.GpuReadbackBytes},
                {"run", r.StepRun},
                {"workspace_builds", r.WorkspaceBuilds},
                {"index_reused", r.IndexReused},
                {"cpu_compute_ms", r.CpuComputeMilliseconds},
                {"gpu_neighborhood_ms", r.GpuNeighborhoodMilliseconds}};
        }

        R::AgentOperationOutcome NoConfig()
        {
            return {.IsError = true, .Text = "The bilateral filter section is missing or invalid.", .ErrorCode = "invalid"};
        }
    }

    void AddTools(R::PluginRegistrations& staged)
    {
        staged.Operations.push_back({
            .Name = "bilateral_filter",
            .Title = "Bilateral point filter",
            .Description =
                "Runs the bilateral point filter with the settings of the config section "
                "sandbox.bilateral_filter (set them with config_apply) and answers when the run ends: it "
                "computes at once and accepts its last pass (max_iterations passes), whatever the section's "
                "start and auto_accept. Status applied, invalid, failed, stale, discarded or cancelled, with "
                "the run's diagnostics and its run id (run). Cancellable; the run is a row of the editor's Jobs "
                "window. bilateral_filter_start starts a run without waiting.",
            .ReadOnly = false,
            .Invoke =
                [](const R::AgentOperationContext&, std::string_view)
            {
                auto config = R::ActivePluginSection<kSection>();
                if (!config)
                    return NoConfig();
                config->Execution.Start = R::StepRunStart::Run;
                config->Execution.AutoAccept = R::StepRunAutoAccept::End;
                const auto run = Start(*config);
                return R::WaitForPluginRun(run, [](const Result& r) { return ResultJson(r).dump(); });
            },
        });
        staged.Operations.push_back({
            .Name = "bilateral_filter_start",
            .Title = "Start a bilateral point filter step run",
            .Description =
                "Starts the bilateral point filter with the section sandbox.bilateral_filter as a step run "
                "(one filter pass a step) and answers its run id at once: the section's execution options "
                "apply unless the call names max_iterations, preview_every, preview_interval_ms, preview_buffers, "
                "auto_accept or start (ready: captured, nothing computed). Drive it with run_control (run, "
                "pause, step, stop, restart, accept, discard) and watch it with run_observe; Accept writes "
                "the output with one undo entry. Live preview shows an in-place output's moving points.",
            .InputSchemaJson = std::string{R"({"type":"object","properties":{)"} +
                R::StepRunExecutionSchemaProperties(kMaxIterations, kMinIterations) +
                R"(},"additionalProperties":false})",
            .ReadOnly = false,
            .Invoke =
                [](const R::AgentOperationContext&, const std::string_view arguments)
            {
                auto config = R::ActivePluginSection<kSection>();
                if (!config)
                    return NoConfig();
                std::string refusal;
                if (!R::ReadStepRunExecutionArguments(
                        arguments, kMaxIterations, config->Execution, refusal, kMinIterations))
                    return R::AgentOperationOutcome{.IsError = true, .Text = refusal, .ErrorCode = "invalid_params"};
                const auto run = Start(*config);
                if (run->Final)
                    return R::AgentOperationOutcome{
                        .IsError = true, .Text = ResultJson(*run->Final).dump(), .ErrorCode = run->Final->Status};
                return R::AgentOperationOutcome{.Text = ResultJson(run->Pending).dump()};
            },
        });
        staged.Operations.push_back({
            .Name = "bilateral_filter_preview",
            .Title = "Bilateral point filter readiness",
            .Description = "Whether bilateral_filter can run with the section's settings, and why not.",
            .Invoke = [](const R::AgentOperationContext&, std::string_view)
            {
                const auto config = R::ActivePluginSection<kSection>();
                if (!config) return NoConfig();
                const auto reason = Preview(*config);
                return R::AgentOperationOutcome{.Text = Json{{"ready", reason.empty()}, {"reason", reason}}.dump()};
            },
        });
    }
}
