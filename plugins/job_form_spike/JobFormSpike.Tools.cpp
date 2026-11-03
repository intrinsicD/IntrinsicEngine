module;

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

module Intrinsic.Plugin.JobFormSpike;

import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.PluginContract;

namespace Intrinsic::Plugins::JobFormSpike
{
    void AddTools(R::PluginRegistrations& staged)
    {
        staged.Operations.push_back({
            .Name = "job_form_spike_start",
            .Title = "Start a coherent point drift step run (job form spike)",
            .Description = "Captures the source and target clouds (their positions property, default v:position) and "
                           "starts a ready rigid CPD run that moves the source onto the target; drive it with "
                           "run_control and watch it with run_observe. Accept writes the source's positions with one "
                           "undo entry. Both clouds are taken in their own object space.",
            .InputSchemaJson = R"({"type":"object","properties":{"source":{"type":"integer","minimum":1,"maximum":4294967295},)"
                               R"("target":{"type":"integer","minimum":1,"maximum":4294967295},"positions":{"type":"string"},)"
                               R"("hold_step_ms":{"type":"integer","minimum":0,"maximum":10000,"description":"Test hook: every step waits this long first."},)"
                               R"("max_iterations":{"type":"integer","minimum":1,"maximum":100000},)"
                               R"("tolerance":{"type":"number","exclusiveMinimum":0},)"
                               R"("accept_on_convergence":{"type":"boolean"},"preview":{"type":"boolean"}},)"
                               R"("required":["source","target"],"additionalProperties":false})",
            .ReadOnly = false,
            .Invoke = [](const R::AgentOperationContext&, const std::string_view arguments)
            {
                const auto args = nlohmann::json::parse(arguments, nullptr, false); // validated against the schema
                Request request{.Source = args.value("source", 0u), .Target = args.value("target", 0u)};
                request.Positions.Name = args.value("positions", request.Positions.Name);
                request.MaxIterations = args.value("max_iterations", request.MaxIterations);
                request.Tolerance = args.value("tolerance", request.Tolerance);
                request.AcceptOnConvergence = args.value("accept_on_convergence", false);
                request.Preview = args.value("preview", true);
                request.HoldStepMs = args.value("hold_step_ms", 0u);
                std::string refusal;
                const auto run = StartRun(request, refusal);
                if (run == 0u) return R::AgentOperationOutcome{.IsError = true, .Text = refusal, .ErrorCode = "refused"};
                return R::AgentOperationOutcome{.Text = nlohmann::json{{"run", run}}.dump()};
            },
        });
    }
}
