module;

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

module Intrinsic.Plugin.CoherentPointDrift;

import Geometry.Registration.CoherentPointDrift;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PointCloudEditing;
import Extrinsic.Runtime.StepRuns;

namespace Intrinsic::Plugins::CoherentPointDrift
{
    namespace
    {
        namespace CPD = Geometry::CoherentPointDrift;

        // A run's own data: the two captures, the solver over world points, and the source's world-to-local map. Step
        // and Preview use the solver (never at once, never beside Commit); Show, Valid and Discard use only the
        // captures and the service.
        struct State
        {
            R::PointCloudEditingService* Editing{};
            std::shared_ptr<R::PointEdit> Source{}, Target{};
            CPD::Solver Solver{};
            glm::dmat4 SourceFromWorld{1.0}; // the full affine inverse of the source's model matrix
        };

        // Points mapped by an affine matrix in double (non-finite results are refused by the service).
        std::vector<glm::vec3> Mapped(const glm::dmat4& m, const auto& points)
        {
            std::vector<glm::vec3> mapped;
            mapped.reserve(points.size());
            for (const auto& p : points) mapped.emplace_back(m * glm::dvec4(glm::dvec3(p), 1.0));
            return mapped;
        }

        bool Finite(const glm::dmat4& m)
        {
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    if (!std::isfinite(m[c][r])) return false;
            return true;
        }

        std::shared_ptr<R::PointEdit> Capture(R::PointCloudEditingService& editing, const std::uint32_t entity,
                                              const R::GeometryPropertyRef& positions, std::string& refusal)
        {
            // In place: the source's output is its positions; the target's output is never written. No normal input;
            // the local Transform is captured (and checked at Show, Commit and every pump).
            return editing.Capture({.StableEntityId = entity, .Positions = positions, .Normals = {}, .Output = positions,
                                    .CaptureTransform = true},
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
        state->SourceFromWorld = glm::inverse(source.Model); // non-finite for a singular or non-finite matrix
        if (!Finite(state->SourceFromWorld))
        {
            refusal = "The source's Transform cannot be inverted (a zero or non-finite scale).";
            return 0u;
        }
        CPD::Params params{.MaxIterations = request.MaxIterations, .Tolerance = request.Tolerance};
        const auto target = editing->View(*state->Target);
        if (const auto status = state->Solver.Initialize(Mapped(target.Model, target.Points), Mapped(source.Model, source.Points),
                                                         params);
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
                values.clear();
                for (const auto& p : Mapped(state->SourceFromWorld, state->Solver.Current().TransformedSource))
                    values.insert(values.end(), {p.x, p.y, p.z});
            },
            .Show = [state](const R::StepPreview& preview)
            {
                std::vector<glm::vec3> points(preview.Values.size() / 3u);
                for (std::size_t i = 0; i < points.size(); ++i)
                    points[i] = {preview.Values[3 * i], preview.Values[3 * i + 1], preview.Values[3 * i + 2]};
                std::string diagnostic;
                switch (state->Editing->PreviewPositions(*state->Source, points, diagnostic))
                {
                case R::PointPreviewStatus::Shown: return R::StepRunShow::Shown;
                case R::PointPreviewStatus::Busy: return R::StepRunShow::Later;
                case R::PointPreviewStatus::Unavailable: break; // no device, a non-finite value: nothing to show
                }
                return R::StepRunShow::Never;
            },
            .Valid = [state] { return state->Editing->Current(*state->Source) && state->Editing->Current(*state->Target); },
            .Commit = [state](std::string& diagnostic)
            {
                const auto status = state->Editing->Publish(
                    *state->Source, Mapped(state->SourceFromWorld, state->Solver.Current().TransformedSource),
                    "Coherent point drift (steps)");
                if (status == R::PointPublishStatus::Applied) return true;
                diagnostic = status == R::PointPublishStatus::Stale ? "stale: the captured inputs changed"
                                                                    : "the positions were refused";
                return false;
            },
            .Discard = [state] { state->Editing->DiscardPreview(*state->Source); },
        };
        if (!request.Preview) callbacks.Preview = {};
        R::StepRunOptions options{.Label = "Coherent point drift (steps)",
                                  .Owner = "coherent_point_drift",
                                  .Output = {.StableEntityId = request.Source, .Property = source.Output},
                                  .IterationCap = request.MaxIterations,
                                  .AcceptOnConvergence = request.AcceptOnConvergence,
                                  .MetricNames = {"sigma2", "objective", "matched", "nll"}};
        const auto run = runs->Start(std::move(options), std::move(callbacks), R::PluginStepRunJobs(), refusal);
        if (run != 0u) g_LastRun = run;
        return run;
    }
}
