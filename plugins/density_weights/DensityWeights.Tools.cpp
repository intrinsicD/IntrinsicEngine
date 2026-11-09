module;

#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

module Intrinsic.Plugin.DensityWeights;

import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PluginSections;

namespace Intrinsic::Plugins::DensityWeights
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
                {"weights", {{"domain", int(r.Weights.Domain)}, {"name", r.Weights.Name}}},
                {"slot_count", r.SlotCount},
                {"live_count", r.LiveCount},
                {"written_count", r.WrittenCount},
                {"support_radius", r.SupportRadius},
                {"query_radius", r.QueryRadius},
                {"min_weight", r.MinWeight},
                {"max_weight", r.MaxWeight},
                {"maximum_neighbors", r.MaximumNeighbors},
                {"neighbor_contributions", r.Diagnostics.NeighborContributionCount},
                {"gpu_query_batches", r.GpuQueryBatches},
                {"gpu_input_upload_bytes", r.GpuInputUploadBytes},
                {"run", r.StepRun},
                {"gpu_input_cache_hits", r.GpuInputCacheHits},
                {"cpu_stage_readback_bytes", r.CpuStageReadbackBytes},
                {"index_reused", r.IndexReused},
                {"cpu_compute_ms", r.CpuComputeMilliseconds}};
        }

        R::AgentOperationOutcome NoConfig()
        {
            return {.IsError = true, .Text = "The density weights section is missing or invalid.", .ErrorCode = "invalid"};
        }
    }

    void AddTools(R::PluginRegistrations& staged)
    {
        staged.Operations.push_back({
            .Name = "density_weights",
            .Title = "Density weights",
            .Description =
                "Computes compact density weights with the settings of the config section "
                "sandbox.density_weights (set them with config_apply). Every backend is a one-shot step run. "
                "Without auto_accept and start it computes at once, accepts at its end and answers when the run "
                "ends: status applied, invalid, busy, failed, stale, discarded or cancelled, with the support "
                "radius used and the weight range. Each of auto_accept and start given replaces the section's; a "
                "run that does not then compute at once and accept by itself answers at once with its run id "
                "(run), which run_control (run, step, pause, stop, restart, accept, discard) and run_observe "
                "drive. Cancellable; the run is a row of the editor's Jobs window.",
            .InputSchemaJson = R::ScalarToolSchemaJson(),
            .ReadOnly = false,
            .Invoke =
                [](const R::AgentOperationContext&, std::string_view arguments)
            {
                auto config = R::ActivePluginSection<kSection>();
                if (!config)
                    return NoConfig();
                return R::RunScalarPluginTool(
                    arguments,
                    config->Execution,
                    [&config](const R::StepRunExecution& execution)
                    {
                        config->Execution = execution;
                        return Start(*config);
                    },
                    [](const Result& r)
                    {
                        return ResultJson(r).dump();
                    },
                    [](Run& run)
                    {
                        Refresh(run);
                    },
                    [](Run& run)
                    {
                        return Cancel(run);
                    });
            },
        });
        staged.Operations.push_back({
            .Name = "density_weights_preview",
            .Title = "Density weights readiness",
            .Description = "Whether density_weights can run with the section's settings, and why not.",
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
