// ARCH-023: a queued CPU editor method as a one-shot worker step run of the session's step-run service (outlier
// analysis through the operation runtime, normal estimation, keypoint analysis, point construction, whose Publish
// creates a new entity), so its CPU backends take the run
// controls and the execution options its GPU transaction takes: the one step computes on a worker, the run's Commit
// (automatic for a batch, else run_control's or a panel's Accept) publishes, a restart computes again from the
// capture, and a run that ends without a commit (discard, cancel, stale captures, a failed step) delivers as a
// cancelled queued job does, at its settled end (after the step returned and the holder released the output). Its
// holder job carries the output identity, so a second run on that output is refused as a duplicate until then.
// Include after `Runtime.GeometryProcessingOperations.JobFailure.hpp` and `import Extrinsic.Runtime.StepRuns;`; the
// including unit supplies array, cstdint, functional, memory, string and utility.
#pragma once

extern "C++"
{
    namespace Extrinsic::Runtime::GeometryProcessingDetail::MeshSupport
    {
        // The run's names, output identity and options (its auto_accept and start apply).
        struct OneShotCpuRunOptions
        {
            std::string Label{}, Owner{}; // the Jobs row and the run's owner
            EditorJobIdentity Identity{};
            StepRunExecution Execution{};
            // Optional (OneShotWorkerCallbacks::Abort): at once when the run ends without a commit, also while the
            // step runs, so a waiting device page gives up; the result still arrives at the settled end.
            std::function<void()> Abort{};
            // The step's metric names (none by default: an unnamed value is not shown), and optionally its values on
            // the worker after a step that succeeded; name only what Metric fills.
            std::array<std::string, 4> MetricNames{};
            std::function<void(StepRunMetric& metric)> Metric{};
        };

        // Starts the run of the captured `w` (a queued editor job's work: its `Result` and `Abandoned`) on
        // `context.StepRuns`. `compute` is the one step on a worker, from the result as captured (a restart computes
        // again from it); `current` checks the captures on the main thread; `publish` is stage 7 at Accept. Answers
        // the Pending result with the run's id (Result.StepRun), or the rejected one; every result `delivery`
        // delivers carries the id too.
        template <class Work, class Result = decltype(std::declval<Work&>().Result)>
        [[nodiscard]] Result StartOneShotCpuRun(const EditorProcessingContext& context,
            OneShotCpuRunOptions options,
            const std::shared_ptr<Work>& w,
            std::function<void(Work&)> compute,
            std::function<bool(const Work&)> current,
            std::function<Result(const std::shared_ptr<Work>&)> publish,
            const QueuedJobDelivery<Result>& delivery)
        {
            const auto id = std::make_shared<std::uint64_t>(0u);
            const auto withId = [id](Result r)
            {
                r.StepRun = *id;
                return r;
            };
            OneShotWorkerCallbacks callbacks{.Compute =
                                                 [w, initial = w->Result, compute = std::move(compute)]
                {
                    w->Result = initial;
                    compute(*w);
                    return w->Result.Status != EditorCommandStatus::GeometryProcessingFailed;
                },
                // `current` checks the attachment first (GeometryPropertiesCurrent), so a detached session is stale.
                .Valid =
                    [w, current = std::move(current)]
                {
                    return !w->Abandoned && current(*w);
                },
                .Publish =
                    [w, publish = std::move(publish), delivery, withId](std::string& diagnostic)
                {
                    auto published = withId(publish(w));
                    if (!published.Succeeded())
                        diagnostic = published.Message;
                    return delivery.Publish(std::move(published));
                },
                // The settled end: the step returned, so a failed step's own result is delivered as it is; any other
                // end delivers the queued snapshot.
                .Ended =
                    [w, delivery, withId](const StepRunObservation& observed)
                {
                    w->Abandoned = true;
                    if (observed.StepFailed)
                        delivery.FinalizeAfterWorker(withId(w->Result));
                    else
                        delivery.FinalizeFrom(withId(delivery.Pending()));
                },
                .Abort = std::move(options.Abort)};
            callbacks.Metric = std::move(options.Metric);
            std::string refusal;
            *id = StartOneShotWorkerRun(*context.StepRuns,
                {.Label = options.Label,
                    .Owner = std::move(options.Owner),
                    .Execution = options.Execution,
                    .MetricNames = std::move(options.MetricNames)},
                std::move(callbacks),
                EditorStepRunJobs(context.JobCommands, std::move(options.Identity), std::make_shared<JobToken>()),
                refusal);
            if (*id == 0u)
            {
                w->Abandoned = true;
                return delivery.Rejected();
            }
            return withId(delivery.Pending());
        }
    }
}
