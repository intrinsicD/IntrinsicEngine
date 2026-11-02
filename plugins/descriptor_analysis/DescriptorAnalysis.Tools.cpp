module;

#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

module Intrinsic.Plugin.DescriptorAnalysis;

import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PluginSections;

namespace Intrinsic::Plugins::DescriptorAnalysis
{
    namespace
    {
        using Json = nlohmann::json;

        Json ResultJson(const Result& r)
        {
            Json outputs = Json::array();
            for (const auto& output : r.Outputs) outputs.push_back(output.Name);
            return {{"status", r.Status},
                    {"message", r.Message},
                    {"requested_backend", ToString(r.RequestedBackend)},
                    {"actual_backend", r.ActualBackend},
                    {"domain", std::string{R::ToString(r.Outputs.front().Domain)}},
                    {"outputs", outputs},
                    {"slot_count", r.SlotCount},
                    {"live_count", r.LiveCount},
                    {"written_count", r.WrittenCount},
                    {"mean_spacing", r.MeanSpacing},
                    {"feature_radius", r.FeatureRadius},
                    {"maximum_neighbors", r.MaximumNeighbors},
                    {"index_reused", r.IndexReused},
                    {"gpu_query_batches", r.GpuQueryBatches},
                    {"gpu_neighborhood_ms", r.GpuNeighborhoodMilliseconds},
                    {"cpu_compute_ms", r.CpuComputeMilliseconds}};
        }

        R::AgentOperationOutcome Answer(const Result& r)
        {
            const bool applied = r.Status == "applied";
            return {.IsError = !applied, .Text = ResultJson(r).dump(), .ErrorCode = applied ? std::string{} : r.Status};
        }

        R::AgentOperationOutcome NoConfig()
        {
            return {.IsError = true, .Text = "The descriptor analysis section is missing or invalid.", .ErrorCode = "invalid"};
        }
    }

    void AddTools(R::PluginRegistrations& staged)
    {
        staged.Operations.push_back({
            .Name = "descriptor_analysis",
            .Title = "FPFH descriptor analysis",
            .Description = "Computes the 33-bin FPFH descriptors with the settings of the config section "
                           "sandbox.descriptor_analysis (set them with config_apply) into its 33 scalar outputs, and "
                           "answers when the run ends: status applied, invalid, failed, stale or cancelled, with the "
                           "run's diagnostics. Cancellable; the run is a row of the editor's Jobs window.",
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
            .Name = "descriptor_analysis_preview",
            .Title = "FPFH descriptor analysis readiness",
            .Description = "Whether descriptor_analysis can run with the section's settings, and why not.",
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
