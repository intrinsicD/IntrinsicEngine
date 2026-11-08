// The executable method template (ARCH-023): a CPU worker step run on the shared driver, and a one-shot method on the
// CPU and on an existing resident GPU producer; see include/TemplateMethod.hpp. The math is the copy's (TemplateMath,
// TemplateOneShotCompute); everything else here is the part to keep.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

import Extrinsic.Runtime.PointCloudEditing;
import Geometry.PointLBVH;

#include "TemplateMethod.hpp"

namespace Intrinsic::Methods::Template
{
    namespace
    {
        namespace L = Geometry::PointLBVH;

        // A run's data. Start, Valid, Commit and Restart run on the main thread; Step on a worker, never beside
        // Commit or Restart (the driver's guarantee), so `State` needs no lock. The driver's callables own it and drop
        // it once the run retired and nothing is in flight.
        struct TemplateRun
        {
            R::PointCloudEditingService* Service{nullptr};
            std::shared_ptr<R::PointEdit> Edit{}; // holds the index lease `Index` points into
            std::vector<glm::vec3> Points{}; // the captured live points (compact indices)
            const L::Index* Index{nullptr}; // CachedLbvh only
            TemplateQuery Query{TemplateQuery::CachedLbvh};
            std::vector<std::uint32_t> Rows{}; // built by the first step, kept across generations
            std::uint32_t K{0u};
            TemplateMath Math{};
            std::vector<float> State{};
            bool Prepared{false}; // this generation's rows and initial state exist (the first step builds them)
            std::string History{};
        };

        // The rows through the chosen query: k compact indices per point, self excluded by index. False when a row is
        // short (fewer eligible points than k).
        bool BuildRows(std::span<const glm::vec3> points,
            const std::uint32_t k,
            const TemplateQuery query,
            const L::Index* index,
            std::vector<std::uint32_t>& rows)
        {
            rows.clear();
            rows.reserve(points.size() * k);
            for (std::uint32_t i = 0; i < points.size(); ++i)
            {
                const auto row = query == TemplateQuery::BruteForce ? L::KNearestReference(points, points[i], k, i)
                                                                    : index->KNearest(points[i], k, i);
                if (row.size() != k)
                    return false;
                for (const auto& neighbor : row)
                    rows.push_back(neighbor.Index);
            }
            return true;
        }

        // The first step of a generation, on the worker: the rows (once per capture) and the initial state. False
        // (the step fails, run_observe: "a step failed") when a row is short or Init gives the wrong size.
        bool Prepare(TemplateRun& run)
        {
            if (run.Rows.empty() && !BuildRows(run.Points, run.K, run.Query, run.Index, run.Rows))
            {
                run.Rows.clear();
                return false;
            }
            run.State = run.Math.Init(run.Points);
            run.Prepared = run.State.size() == run.Points.size();
            return run.Prepared;
        }
    }

    std::uint64_t StartTemplateCpuRun(R::PointCloudEditingService& service,
        R::StepRunsService& runs,
        R::StepRunJobs jobs,
        const TemplateRequest& request,
        TemplateMath math,
        std::string& refusal)
    {
        const auto fail = [&](std::string why)
        {
            refusal = std::move(why);
            return std::uint64_t{0u};
        };
        if (!math.Init || !math.Step)
            return fail("The template needs its Init and Step math.");
        if (request.K == 0u)
            return fail("k must be at least 1.");
        // Cap 0 is refused: the first step prepares the state Commit publishes (method-template.md, max_iterations 0).
        if (const auto why = R::StepRunExecutionRefusal(request.Execution, UINT32_MAX); !why.empty())
            return fail("Template: " + std::string{why});
        // Init: the capture only (the service checks entity, domains and output); the rows and the state are the first
        // step's, on the worker, so neither an O(n^2) brute-force scan nor the copy's Init runs on the main thread.
        auto run = std::make_shared<TemplateRun>();
        run->Service = &service;
        run->K = request.K;
        run->Math = std::move(math);
        run->History = request.History;
        const bool lbvh = request.Query == TemplateQuery::CachedLbvh;
        run->Edit = service.Capture({.StableEntityId = request.StableEntityId,
                                        .Positions = request.Positions,
                                        .ScalarOutputs = {request.Output},
                                        .MinimumLiveCount = std::size_t{request.K} + 1u,
                                        .Noun = "Template",
                                        .AcquireIndex = lbvh},
            refusal);
        if (!run->Edit)
            return 0u;
        const auto view = service.View(*run->Edit);
        run->Points.assign(view.Points.begin(), view.Points.end());
        run->Query = request.Query;
        run->Index = lbvh ? service.CpuIndex(*run->Edit) : nullptr;
        if (lbvh && run->Index == nullptr)
            return fail("The cached LBVH of the capture is unavailable.");
        // No live preview: the editing service displays positions only, so a scalar state copy would have no consumer.
        // run_observe reports the normalized preview_every 0 (CPU smoothing does the same).
        auto execution = request.Execution;
        execution.PreviewEvery = 0u;
        execution.PreviewIntervalMs = 0u;

        // Step: one logical iteration (the first of a generation prepares); its metric is the largest change.
        R::StepRunCallbacks callbacks{.Step =
                                          [run](R::StepRunMetric& metric)
            {
                if (!run->Prepared && !Prepare(*run))
                    return R::StepOutcome::Failed;
                const std::vector<float> before = run->State;
                const bool converged = run->Math.Step(run->State, run->Rows, run->K);
                double change = 0.0;
                for (std::size_t i = 0; i < before.size(); ++i)
                    change = std::max(change, std::abs(double(run->State[i]) - double(before[i])));
                metric.Values[0] = change;
                return converged ? R::StepOutcome::Converged : R::StepOutcome::Continue;
            },
            // Generation safety: the capture's inputs and output are unchanged (a stale run is discarded).
            .Valid =
                [run]
            {
                return run->Service->Current(*run->Edit);
            },
            // Publishes once, one undo entry; the service checks the capture's revisions again.
            .Commit =
                [run](std::string& diagnostic)
            {
                const std::vector<std::vector<float>> columns{run->State};
                switch (run->Service->PublishScalars(*run->Edit, columns, run->History))
                {
                case R::PointPublishStatus::Applied:         return R::StepRunCommit::Committed;
                case R::PointPublishStatus::Stale:           diagnostic = "stale: the input or the output changed"; break;
                case R::PointPublishStatus::Unrepresentable: diagnostic = "a value does not fit the output"; break;
                case R::PointPublishStatus::Rejected:        diagnostic = "the history refused the publication"; break;
                }
                return R::StepRunCommit::Refused;
            },
            // A new generation from the same capture and rows: its first step prepares the state again.
            .Restart =
                [run](std::string&)
            {
                run->Prepared = false;
                return true;
            }};
        return runs.Start({.Label = "Template method",
                              .Owner = "methods/_template",
                              .Output = R::PluginJobOutput{.StableEntityId = request.StableEntityId,
                                  .Property = view.ScalarOutputs.front()},
                              .Execution = execution,
                              .MetricNames = {"max_change"}},
            std::move(callbacks),
            std::move(jobs),
            refusal);
    }

    std::uint64_t StartTemplateOneShotCpuRun(R::PointCloudEditingService& service,
        R::StepRunJobs jobs,
        const TemplateOneShotRequest& request,
        TemplateOneShotCompute compute,
        std::function<void(std::string_view status, std::string message, bool ownResult)> finished,
        std::string& refusal)
    {
        if (!compute || !finished || request.Params.K == 0u)
        {
            refusal = "The one-shot template needs its Compute, its end and k >= 1.";
            return 0u;
        }
        const bool lbvh = request.Query == TemplateQuery::CachedLbvh;
        // Init: the capture, as the iterative run's; the rows and the values are the one step's (on the worker).
        auto edit = service.Capture({.StableEntityId = request.StableEntityId,
                                        .Positions = request.Positions,
                                        .ScalarOutputs = {request.Output},
                                        .MinimumLiveCount = std::size_t{request.Params.K} + 1u,
                                        .Noun = "Template",
                                        .AcquireIndex = lbvh},
            refusal);
        if (!edit)
            return 0u;
        const auto view = service.View(*edit);
        auto points = std::make_shared<const std::vector<glm::vec3>>(view.Points.begin(), view.Points.end());
        const L::Index* index = lbvh ? service.CpuIndex(*edit) : nullptr;
        if (lbvh && index == nullptr)
        {
            refusal = "The cached LBVH of the capture is unavailable.";
            return 0u;
        }
        return service.StartScalarCpuRun(
            {.Edit = edit,
                .Session = service.CurrentSession(),
                .Output = {.StableEntityId = request.StableEntityId, .Property = view.ScalarOutputs.front()},
                .Label = "Template one-shot method",
                .History = request.History,
                .Noun = "Template",
                .Method = "template",
                .Execution = request.Execution,
                // The one step: the rows from the capture again on every generation (a restart computes the same).
                // `index` is the capture's index lease: `edit` is captured with it so the raw pointer never outlives
                // its owner, whatever else drops the capture.
                .Compute =
                    [edit, points, index, k = request.Params.K, query = request.Query, compute = std::move(compute)](
                        std::vector<float>& values)
                {
                    std::vector<std::uint32_t> rows;
                    return BuildRows(*points, k, query, index, rows) && compute(*points, rows, k, values) &&
                        values.size() == points->size();
                },
                // After the step returned: only now may the copy free or reuse what its Compute used. The next run on
                // the output: from inside it at a settled end (no publication); for a publication only once it returned.
                .Finish = std::move(finished)},
            std::move(jobs),
            refusal);
    }

    R::ScalarAnalysis StartTemplateOneShotGpuRun(
        R::PointCloudEditingService& service, const TemplateOneShotRequest& request, std::string& refusal)
    {
        R::ScalarAnalysisRefusal why;
        auto run = service.StartScalarAnalysis({.StableEntityId = request.StableEntityId,
                                                   .Positions = request.Positions,
                                                   .Output = request.Output,
                                                   .Params = request.Params,
                                                   .Noun = "Template",
                                                   .Method = "Template",
                                                   .History = request.History},
            request.Execution,
            why);
        refusal = std::move(why.Message);
        return run;
    }
}
