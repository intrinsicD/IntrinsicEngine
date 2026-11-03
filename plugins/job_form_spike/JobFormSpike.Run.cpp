module;

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <glm/vec3.hpp>

module Intrinsic.Plugin.JobFormSpike;

import Geometry.Registration.CoherentPointDrift;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PointCloudEditing;
import Extrinsic.Runtime.StepRuns;

namespace Intrinsic::Plugins::JobFormSpike
{
    namespace
    {
        namespace CPD = Geometry::CoherentPointDrift;

        // A run's own data: the two captures and the solver. Step and Preview use the solver (never at once, never
        // beside Commit); Show, Valid and Discard use only the captures and the service.
        struct State
        {
            R::PointCloudEditingService* Editing{};
            std::shared_ptr<R::PointEdit> Source{}, Target{};
            CPD::Solver Solver{};
        };

        std::shared_ptr<R::PointEdit> Capture(R::PointCloudEditingService& editing, const std::uint32_t entity,
                                              const R::GeometryPropertyRef& positions, std::string& refusal)
        {
            // In place: the source's output is its positions; the target's output is never written.
            // shortcut: the service's capture always takes a vec3 normal input; CPD reads none, so the positions stand
            // in. Give the service an optional normal input if the spike becomes the CPD plugin.
            return editing.Capture({.StableEntityId = entity, .Positions = positions, .Normals = positions,
                                    .Output = positions},
                                   refusal);
        }

        std::uint64_t g_LastRun{0u};
    }

    std::uint64_t LastRun() noexcept { return g_LastRun; }

    std::uint64_t StartRun(const Request& request, std::string& refusal)
    {
        auto* editing = R::PluginService<R::PointCloudEditingService>();
        auto* runs = R::PluginService<R::StepRunsService>();
        if (editing == nullptr || runs == nullptr)
        {
            refusal = "The point-cloud editing or step-run service is unavailable.";
            return 0u;
        }
        if (request.Source == request.Target)
        {
            refusal = "Choose two different clouds.";
            return 0u;
        }
        auto state = std::make_shared<State>();
        state->Editing = editing;
        state->Source = Capture(*editing, request.Source, request.Positions, refusal);
        if (!state->Source) return 0u;
        state->Target = Capture(*editing, request.Target, request.Positions, refusal);
        if (!state->Target) return 0u;
        const auto source = editing->View(*state->Source);
        CPD::Params params{.MaxIterations = request.MaxIterations, .Tolerance = request.Tolerance};
        if (const auto status = state->Solver.Initialize(editing->View(*state->Target).Points, source.Points, params);
            status != CPD::Status::Success)
        {
            refusal = "Coherent point drift: " + std::string{CPD::ToString(status)};
            return 0u;
        }

        R::StepRunCallbacks callbacks{
            .Step = [state, hold = std::chrono::milliseconds{request.HoldStepMs}](R::StepRunMetric& metric)
            {
                std::this_thread::sleep_for(hold);
                CPD::IterationTrace trace{};
                bool iterated = false;
                const bool more = state->Solver.Step([&](const CPD::IterationTrace& t) { trace = t, iterated = true; });
                metric.Values = {trace.Sigma2, trace.Objective, trace.MatchedWeight, trace.NegativeLogLikelihood};
                if (more) return R::StepOutcome::Continue;
                const auto result = state->Solver.Current(); // once, at the end
                if (!result.Succeeded()) return R::StepOutcome::Failed;
                // The solver detects convergence in the next call's E-step, which ends without an iteration: its
                // metric is that E-step's (rigid: the objective is the negative log-likelihood).
                if (!iterated)
                    metric.Values = {result.Sigma2, result.NegativeLogLikelihood, result.MatchedWeight,
                                     result.NegativeLogLikelihood};
                // The solver's cap is the run's: the run ends it at the same step.
                return result.Stop == CPD::Termination::IterationCap ? R::StepOutcome::Continue : R::StepOutcome::Converged;
            },
            .Preview = [state](std::vector<float>& values)
            {
                const auto result = state->Solver.Current();
                values.clear();
                for (const auto& p : result.TransformedSource)
                    values.insert(values.end(), {float(p.x), float(p.y), float(p.z)});
            },
            .Show = [state](const R::StepPreview& preview)
            {
                std::vector<glm::vec3> points(preview.Values.size() / 3u);
                for (std::size_t i = 0; i < points.size(); ++i)
                    points[i] = {preview.Values[3 * i], preview.Values[3 * i + 1], preview.Values[3 * i + 2]};
                std::string diagnostic; // busy: offered again; unavailable (no device): nothing to show
                return state->Editing->PreviewPositions(*state->Source, points, diagnostic) != R::PointPreviewStatus::Busy;
            },
            .Valid = [state] { return state->Editing->Current(*state->Source) && state->Editing->Current(*state->Target); },
            .Commit = [state](std::string& diagnostic)
            {
                const auto result = state->Solver.Current();
                const std::vector<glm::vec3> points(result.TransformedSource.begin(), result.TransformedSource.end());
                const auto status = state->Editing->Publish(*state->Source, points, "Coherent point drift (job form spike)");
                if (status == R::PointPublishStatus::Applied) return true;
                diagnostic = status == R::PointPublishStatus::Stale ? "stale: the captured inputs changed"
                                                                    : "the positions were refused";
                return false;
            },
            .Discard = [state] { state->Editing->DiscardPreview(*state->Source); },
        };
        if (!request.Preview) callbacks.Preview = {};
        R::StepRunOptions options{.Label = "Coherent point drift (job form spike)",
                                  .Owner = "job_form_spike",
                                  .Output = {.StableEntityId = request.Source, .Property = source.Output},
                                  .IterationCap = request.MaxIterations,
                                  .AcceptOnConvergence = request.AcceptOnConvergence,
                                  .MetricNames = {"sigma2", "objective", "matched", "nll"}};
        const auto run = runs->Start(std::move(options), std::move(callbacks), R::PluginStepRunJobs(), refusal);
        if (run != 0u) g_LastRun = run;
        return run;
    }
}
