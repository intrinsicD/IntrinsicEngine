module;

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

module Intrinsic.Plugin.CoherentPointDrift;

import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.StepRuns;

namespace Intrinsic::Plugins::CoherentPointDrift
{
    void AddTools(R::PluginRegistrations& staged)
    {
        staged.Operations.push_back({
            .Name = "coherent_point_drift_start",
            .Title = "Start a coherent point drift step run",
            .Description = "Captures the source and target clouds (their positions property, default v:position) and "
                           "starts a rigid CPD run that moves the source onto the target, ready by default (start: "
                           "run computes at once); drive it with run_control (run, pause, step, stop, restart, accept, "
                           "discard) and watch it with run_observe. The shared execution options are max_iterations, "
                           "preview_every (0: no live preview), preview_interval_ms, auto_accept and start. Accept "
                           "writes the source's positions with one undo entry. Both clouds must be unparented; each is "
                           "mapped by its local Transform (world space, as the editor's CPD).",
            .InputSchemaJson =
                std::string{
                    R"({"type":"object","properties":{"source":{"type":"integer","minimum":1,"maximum":4294967295},)"
                    R"("target":{"type":"integer","minimum":1,"maximum":4294967295},"positions":{"type":"string"},)"
                    R"("hold_step_ms":{"type":"integer","minimum":0,"maximum":10000,"description":"Test hook: every step waits this long first."},)"
                    R"("tolerance":{"type":"number","exclusiveMinimum":0},)"} +
                R::StepRunExecutionSchemaProperties(kMaxIterations) +
                R"(},"required":["source","target"],"additionalProperties":false})",
            .ReadOnly = false,
            .Invoke =
                [](const R::AgentOperationContext&, const std::string_view arguments)
            {
                const auto args = nlohmann::json::parse(arguments, nullptr, false); // validated against the schema
                Request request{.Source = args.value("source", 0u), .Target = args.value("target", 0u)};
                request.Positions.Name = args.value("positions", request.Positions.Name);
                request.Tolerance = args.value("tolerance", request.Tolerance);
                request.HoldStepMs = args.value("hold_step_ms", 0u);
                std::string refusal;
                if (!R::ReadStepRunExecutionArguments(arguments, kMaxIterations, request.Execution, refusal))
                    return R::AgentOperationOutcome{.IsError = true, .Text = refusal, .ErrorCode = "invalid_params"};
                const auto run = StartRun(request, refusal);
                if (run == 0u)
                    return R::AgentOperationOutcome{.IsError = true, .Text = refusal, .ErrorCode = "refused"};
                return R::AgentOperationOutcome{.Text = nlohmann::json{{"run", run}}.dump()};
            },
        });
    }
}
