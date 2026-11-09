module;

#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

module Intrinsic.Plugin.DescriptorAnalysis;

import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PluginSections;
import Extrinsic.Runtime.StepRuns;

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
                {"gpu_query_upload_bytes", r.GpuQueryUploadBytes},
                {"gpu_readback_bytes", r.GpuReadbackBytes},
                {"run", r.StepRun},
                {"gpu_neighborhood_ms", r.GpuNeighborhoodMilliseconds},
                {"cpu_compute_ms", r.CpuComputeMilliseconds}};
        }

        // The start tool's arguments: the two execution rows the section binds (StepRunExecutionFields' auto_accept
        // and start), as the shared schema writes them.
        std::string StartProperties()
        {
            const auto all = Json::parse("{" + R::StepRunExecutionSchemaProperties(1u) + "}");
            const Json chosen{{"auto_accept", all.at("auto_accept")}, {"start", all.at("start")}};
            const auto text = chosen.dump();
            return text.substr(1, text.size() - 2);
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
                           "answers when the run ends: it computes at once and accepts, whatever the section's start "
                           "and auto_accept. Status applied, invalid, failed, stale, discarded or cancelled, with the "
                           "run's diagnostics and its run id (run). Cancellable; the run is a row of the editor's Jobs "
                           "window. descriptor_analysis_start starts a run without waiting.",
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
            .Name = "descriptor_analysis_start",
            .Title = "Start an FPFH descriptor step run",
            .Description = "Starts the FPFH descriptor analysis with the section sandbox.descriptor_analysis as a "
                           "one-step step run and answers its run id at once: the section's auto_accept and start "
                           "apply unless the call names them (start ready: captured, nothing computed). Drive it with "
                           "run_control (run or step 1 computes, restart, accept, discard) and watch it with "
                           "run_observe; Accept writes the 33 columns with one undo entry, Discard writes nothing.",
            .InputSchemaJson = std::string{R"({"type":"object","properties":{)"} + StartProperties() +
                R"(},"additionalProperties":false})",
            .ReadOnly = false,
            .Invoke =
                [](const R::AgentOperationContext&, const std::string_view arguments)
            {
                auto config = R::ActivePluginSection<kSection>();
                if (!config)
                    return NoConfig();
                std::string refusal;
                // One step: the cap and the preview rows are not this run's (the schema offers no such argument).
                if (!R::ReadStepRunExecutionArguments(arguments, 1u, config->Execution, refusal))
                    return R::AgentOperationOutcome{.IsError = true, .Text = refusal, .ErrorCode = "invalid_params"};
                const auto run = Start(*config);
                if (run->Final)
                    return R::AgentOperationOutcome{
                        .IsError = true, .Text = ResultJson(*run->Final).dump(), .ErrorCode = run->Final->Status};
                return R::AgentOperationOutcome{.Text = ResultJson(run->Pending).dump()};
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
