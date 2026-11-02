module;

#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

module Intrinsic.Plugin.BilateralFilter;

import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PluginSections;

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
                    {"workspace_builds", r.WorkspaceBuilds},
                    {"index_reused", r.IndexReused},
                    {"cpu_compute_ms", r.CpuComputeMilliseconds},
                    {"gpu_neighborhood_ms", r.GpuNeighborhoodMilliseconds}};
        }

        R::AgentOperationOutcome Answer(const Result& r)
        {
            const bool applied = r.Status == "applied";
            return {.IsError = !applied, .Text = ResultJson(r).dump(), .ErrorCode = applied ? std::string{} : r.Status};
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
            .Description = "Runs the bilateral point filter with the settings of the config section "
                           "sandbox.bilateral_filter (set them with config_apply) and answers when the run ends: "
                           "status applied, invalid, failed, stale or cancelled, with the run's diagnostics. "
                           "Cancellable; the run is a row of the editor's Jobs window.",
            .ReadOnly = false,
            .Invoke = [](const R::AgentOperationContext&, std::string_view)
            {
                const auto config = R::ActivePluginSection<kSection>();
                if (!config) return NoConfig();
                const auto run = Start(*config);
                if (run->Final) return Answer(*run->Final);
                R::AgentOperationOutcome outcome{.Text = ResultJson(run->Pending).dump()};
                outcome.Continuation = [run](const R::AgentOperationContext&, R::AgentOperationOutcome& result)
                {
                    if (!run->Final) return false;
                    result = Answer(*run->Final);
                    return true;
                };
                outcome.Cancel = [run](const R::AgentOperationContext&)
                { return std::string{Cancel(*run) ? "cancel requested" : "the run is not active"}; };
                return outcome;
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
