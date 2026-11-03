// ARCH-022 S3: the step-run driver with a deterministic step, the real job service and a test owner that carries the
// run's jobs as the plugin host does (joined jobs, a run cancel cancels them all, a refused publication is not
// finalised twice). Each test fails without the mechanism it names.
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

import Extrinsic.Core.Tasks;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.StepRuns;

namespace R = Extrinsic::Runtime;
using Json = nlohmann::json;

namespace
{
    using namespace std::chrono_literals;

    class SchedulerScope final
    {
    public:
        SchedulerScope()
        {
            if (Extrinsic::Core::Tasks::Scheduler::IsInitialized()) Extrinsic::Core::Tasks::Scheduler::Shutdown();
            Extrinsic::Core::Tasks::Scheduler::Initialize(2);
        }
        ~SchedulerScope()
        {
            Extrinsic::Core::Tasks::Scheduler::WaitForAll();
            Extrinsic::Core::Tasks::Scheduler::Shutdown();
        }
    };

    // Carries jobs as the plugin host does: a job joins its run, a cancel cancels the run's jobs, Completion runs
    // once (true when published, false when cancelled or refused).
    struct Owner
    {
        R::JobService Jobs{};
        R::KernelEventBus Events{};
        std::map<R::JobToken, std::vector<R::JobToken>> RunJobs{};
        std::uint64_t ApplyBudget{0u}, GateBudget{0u};
        int Submitted{0};

        R::StepRunJobs Bind()
        {
            return {.Submit = [this](R::PluginJob job, std::string*) { return Submit(std::move(job)); },
                    .Cancel = [this](const R::PluginJobHandle job) { return Cancel(job); }};
        }

        R::PluginJobHandle Submit(R::PluginJob job)
        {
            struct Held
            {
                R::PluginJob Job;
                bool Finalized{false};
            };
            auto held = std::make_shared<Held>(Held{.Job = std::move(job)});
            R::JobDesc desc{};
            desc.DebugName = held->Job.Label;
            desc.Work = [held](const R::JobCancellation& cancellation)
            {
                held->Job.Work([&cancellation] { return cancellation.IsCancelled(); });
                return R::JobResultEnvelope::Make(true);
            };
            if (held->Job.IsReadyToApply) desc.IsReadyToApply = [held] { return held->Job.IsReadyToApply(); };
            desc.PublishCompletion = [held](R::KernelEventBus&, const R::JobResultEnvelope&)
            {
                held->Finalized = true;
                return held->Job.Completion(true);
            };
            desc.FinalizeUnpublishedOnMainThread = [held]
            {
                if (held->Finalized) return;
                held->Finalized = true;
                (void)held->Job.Completion(false);
            };
            const R::JobToken token = Jobs.Submit(std::move(desc));
            if (!token.IsValid()) return {};
            ++Submitted;
            const R::JobToken run = held->Job.Run.IsValid() ? held->Job.Run.Token : token;
            RunJobs[run].push_back(token);
            return {token};
        }

        bool Cancel(const R::PluginJobHandle job)
        {
            bool any = false;
            for (const auto token : RunJobs[job.Token]) any |= Jobs.Cancel(token);
            return any;
        }

        void Frame() { (void)Jobs.DrainCompletions(Events, ApplyBudget, GateBudget); }

        // Frames until `done` holds (a worker finishes a segment in between); false on the deadline.
        bool FramesUntil(const std::function<bool()>& done, const std::chrono::milliseconds limit = 10s)
        {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (!done())
            {
                if (std::chrono::steady_clock::now() > deadline) return false;
                Frame();
                std::this_thread::sleep_for(100us);
            }
            return true;
        }
    };

    // x <- x/2 + 1 from x = 0 converges to 2; converged once the change is below the tolerance. Values: x and |dx|.
    struct Problem
    {
        double X{0.0};
        double Tolerance{1.0e-9};
        std::atomic<int> Active{0}, MaxActive{0};
        std::function<void()> InsideStep{}; // runs inside a step (a test blocks it there)
    };

    struct Probe
    {
        int Commits{0}, Discards{0}, Shows{0}, ShowAttempts{0};
        int ShowRefusals{0}; // Show answers Later this many times first (no free slot, say)
        bool ShowNever{false}; // Show cannot show anything (no device, say)
        std::vector<std::uint64_t> ShownGenerations{};
        std::vector<std::uint32_t> ShownSteps{};
        bool CommitResult{true};
        bool Valid{true};
        std::weak_ptr<Problem> Alive{}; // expires once the run dropped its step callable
    };

    R::StepRunCallbacks Callbacks(const std::shared_ptr<Problem>& problem, Probe& probe)
    {
        probe.Alive = problem;
        return {
            .Step = [problem](R::StepRunMetric& metric)
            {
                const int active = ++problem->Active;
                int seen = problem->MaxActive.load();
                while (active > seen && !problem->MaxActive.compare_exchange_weak(seen, active)) {}
                if (problem->InsideStep) problem->InsideStep();
                const double next = problem->X * 0.5 + 1.0;
                const double change = std::abs(next - problem->X);
                problem->X = next;
                metric.Values = {next, change, 0.0, 0.0};
                --problem->Active;
                return change < problem->Tolerance ? R::StepOutcome::Converged : R::StepOutcome::Continue;
            },
            .Preview = [problem](std::vector<float>& values) { values.assign(1, static_cast<float>(problem->X)); },
            .Show = [&probe](const R::StepPreview& preview)
            {
                ++probe.ShowAttempts;
                if (probe.ShowNever) return R::StepRunShow::Never;
                if (probe.ShowRefusals > 0)
                {
                    --probe.ShowRefusals;
                    return R::StepRunShow::Later;
                }
                ++probe.Shows;
                probe.ShownGenerations.push_back(preview.Generation);
                probe.ShownSteps.push_back(preview.Step);
                return R::StepRunShow::Shown;
            },
            .Valid = [&probe] { return probe.Valid; },
            .Commit = [&probe](std::string& diagnostic)
            {
                ++probe.Commits;
                if (!probe.CommitResult) diagnostic = "stale: refused by the owner";
                return probe.CommitResult;
            },
            .Discard = [&probe] { ++probe.Discards; },
        };
    }

    struct Fixture
    {
        SchedulerScope Scheduler{};
        Owner Jobs{};
        R::StepRunsService Runs{};

        std::uint64_t Start(const std::shared_ptr<Problem>& problem, Probe& probe, R::StepRunOptions options = {},
                            const bool preview = true)
        {
            if (options.Label.empty()) options.Label = "fixed point";
            options.Owner = "test";
            options.MetricNames = {"x", "change", "", ""};
            auto callbacks = Callbacks(problem, probe);
            if (!preview) callbacks.Preview = {};
            std::string refusal;
            const auto id = Runs.Start(std::move(options), std::move(callbacks), Jobs.Bind(), refusal);
            EXPECT_NE(id, 0u) << refusal;
            return id;
        }

        R::StepRunObservation Observe(const std::uint64_t id) { return *Runs.Observe(id); }

        bool Control(const std::uint64_t id, const R::StepRunAction action, const std::uint32_t count = 1u)
        {
            std::string refusal;
            return Runs.Control(id, action, count, refusal);
        }

        // Frames until the run stops computing (paused, ended or terminal).
        bool Settle(const std::uint64_t id)
        {
            return Jobs.FramesUntil([&]
            {
                const auto run = Observe(id);
                return !run.SegmentInFlight && run.Phase != R::StepRunPhase::Running;
            });
        }

        std::vector<std::pair<std::uint32_t, double>> Trace(const std::uint64_t id)
        {
            std::vector<std::pair<std::uint32_t, double>> trace;
            for (const auto& metric : Observe(id).Metrics->Records()) trace.emplace_back(metric.Step, metric.Values[0]);
            return trace;
        }
    };

    std::vector<std::pair<std::uint32_t, double>> Uninterrupted(R::StepRunOptions options = {})
    {
        auto problem = std::make_shared<Problem>();
        Probe probe;
        Fixture f;
        const auto id = f.Start(problem, probe, std::move(options));
        EXPECT_TRUE(f.Control(id, R::StepRunAction::Run));
        EXPECT_TRUE(f.Settle(id));
        EXPECT_EQ(f.Observe(id).End, R::StepRunEnd::Converged);
        return f.Trace(id);
    }
}

TEST(StepRuns, ARunEqualsItsStepSequenceWhateverTheSegments)
{
    const auto reference = Uninterrupted();
    ASSERT_GT(reference.size(), 20u);
    for (std::uint32_t i = 0; i < reference.size(); ++i) ASSERT_EQ(reference[i].first, i + 1u) << "steps 1..N once each";

    {   // step(1) until it ends
        auto problem = std::make_shared<Problem>();
        Probe probe;
        Fixture f;
        const auto id = f.Start(problem, probe);
        while (f.Observe(id).End == R::StepRunEnd::None)
        {
            ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, 1u));
            ASSERT_TRUE(f.Settle(id));
        }
        EXPECT_EQ(f.Trace(id), reference);
        EXPECT_EQ(problem->MaxActive.load(), 1) << "never two steps at once";
    }
    {   // step(7), then run
        auto problem = std::make_shared<Problem>();
        Probe probe;
        Fixture f;
        const auto id = f.Start(problem, probe);
        ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, 7u));
        ASSERT_TRUE(f.Settle(id));
        EXPECT_EQ(f.Observe(id).Steps, 7u);
        ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
        ASSERT_TRUE(f.Settle(id));
        EXPECT_EQ(f.Trace(id), reference);
    }
    {   // run with pauses, each requested while every fifth step is held inside the owner's code
        std::atomic<std::uint32_t> calls{0u}, held{0u}, released{0u};
        auto problem = std::make_shared<Problem>();
        Probe probe;
        Fixture f;
        problem->InsideStep = [&]
        {
            const std::uint32_t call = ++calls;
            if (call % 5u != 0u) return;
            held = call;
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (released.load() < call && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(50us);
        };
        const auto id = f.Start(problem, probe);
        ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
        int pauses = 0;
        for (std::uint32_t next = 5u;; next += 5u)
        {
            ASSERT_TRUE(f.Jobs.FramesUntil([&] { return held.load() >= next || !f.Observe(id).SegmentInFlight &&
                                                     f.Observe(id).End != R::StepRunEnd::None; }));
            if (held.load() < next) break; // it ended before the next held step
            ASSERT_TRUE(f.Control(id, R::StepRunAction::Pause));
            released = next;
            ASSERT_TRUE(f.Settle(id));
            EXPECT_EQ(f.Observe(id).Steps, next) << "the pause lands after the step in flight";
            ++pauses;
            if (f.Observe(id).End != R::StepRunEnd::None) break;
            ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
        }
        EXPECT_GT(pauses, 3);
        EXPECT_EQ(f.Trace(id), reference);
    }
}

TEST(StepRuns, StepCountsExactlyAndAPausedRunDoesNotAdvance)
{
    auto problem = std::make_shared<Problem>();
    Probe probe;
    Fixture f;
    const auto id = f.Start(problem, probe);
    EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Ready);
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, 5u));
    ASSERT_TRUE(f.Settle(id));
    EXPECT_EQ(f.Observe(id).Steps, 5u);
    EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Paused);
    for (int i = 0; i < 20; ++i) f.Jobs.Frame();
    EXPECT_EQ(f.Observe(id).Steps, 5u) << "paused: no step";
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, 3u));
    ASSERT_TRUE(f.Settle(id));
    EXPECT_EQ(f.Observe(id).Steps, 8u);
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
    ASSERT_TRUE(f.Settle(id));
    EXPECT_EQ(f.Observe(id).End, R::StepRunEnd::Converged);
    EXPECT_GT(f.Observe(id).Steps, 8u) << "resumed";
}

TEST(StepRuns, PreviewCopiesAreLimitedAcrossSegmentsAndDoNotChangeTheRun)
{
    const auto reference = Uninterrupted();
    // A controlled clock: frozen unless the test moves it.
    auto now = std::make_shared<std::atomic<std::int64_t>>(0);
    const auto clock = [now] { return std::chrono::steady_clock::time_point{std::chrono::milliseconds{now->load()}}; };
    const auto stepped = [&](const bool preview, const bool frozen)
    {
        auto problem = std::make_shared<Problem>();
        Probe probe;
        Fixture f;
        const auto id = f.Start(problem, probe, {.Clock = clock}, preview);
        int frames = 0;
        while (f.Observe(id).End == R::StepRunEnd::None)
        {
            EXPECT_TRUE(f.Control(id, R::StepRunAction::Step, 1u));
            EXPECT_TRUE(f.Settle(id));
            if (!frozen) now->fetch_add(10); // 10 ms a frame, the interval is 33 ms
            ++frames;
        }
        for (int i = 0; i < 5; ++i) f.Jobs.Frame();
        EXPECT_EQ(f.Trace(id), reference) << "preview " << preview;
        return std::pair{f.Observe(id).PreviewCopies, frames};
    };
    EXPECT_EQ(stepped(false, false).first, 0u) << "preview off: no copy";
    EXPECT_EQ(stepped(true, true).first, 1u) << "a frozen clock: the first copy only";
    const auto [copies, frames] = stepped(true, false);
    EXPECT_LE(copies, static_cast<std::uint64_t>(frames * 10 / 33 + 1)) << "at most one copy per 33 ms";
    EXPECT_GE(copies, 3u);

    // A stalled reader: the run computes to its end without a frame; the writer copies once and never waits.
    auto problem = std::make_shared<Problem>();
    Probe probe;
    Fixture f;
    const auto id = f.Start(problem, probe, {.PreviewInterval = std::chrono::nanoseconds{0}});
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
    f.Jobs.Frame(); // the holder's pump submits the segment
    ASSERT_TRUE(f.Jobs.FramesUntil([&] { return f.Observe(id).SegmentInFlight; }));
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (f.Observe(id).Steps < reference.size() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms); // no frame: the reader is stalled
    EXPECT_EQ(f.Observe(id).Steps, reference.size());
    EXPECT_EQ(f.Observe(id).PreviewCopies, 1u) << "the reader never took the first copy";
    ASSERT_TRUE(f.Settle(id));
    EXPECT_EQ(probe.ShownSteps.front(), 1u) << "the stalled copy shows once the reader runs";
    for (int i = 0; i < 5; ++i) f.Jobs.Frame();
    EXPECT_EQ(probe.ShownSteps.back(), reference.size()) << "then the ended state";
}

TEST(StepRuns, CancelIsTerminalAndTheOwnersCodeGoesAfterItsLastStep)
{
    std::mutex mutex;
    std::condition_variable wake;
    bool inside = false, release = false;
    auto problem = std::make_shared<Problem>();
    Probe probe;
    Fixture f;
    problem->InsideStep = [&]
    {
        std::unique_lock lock(mutex);
        inside = true;
        wake.notify_all();
        wake.wait_for(lock, 10s, [&] { return release; });
    };
    auto clockOwner = std::make_shared<int>(0);
    const std::weak_ptr<int> clockAlive = clockOwner;
    const auto id = f.Start(problem, probe, {.Clock = [clockOwner] { return std::chrono::steady_clock::now(); }});
    problem.reset(); // the run's step callable holds the problem now
    clockOwner.reset();
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
    ASSERT_TRUE(f.Jobs.FramesUntil([&] { std::lock_guard lock(mutex); return inside; }));

    // The Jobs window's cancel: the holder finishes first, while the step is still inside the owner's code.
    ASSERT_TRUE(f.Jobs.Cancel({f.Jobs.RunJobs.begin()->second.front()}));
    for (int i = 0; i < 5; ++i) f.Jobs.Frame();
    EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Discarded);
    EXPECT_EQ(probe.Discards, 1) << "what the run shows goes at once";
    EXPECT_FALSE(probe.Alive.expired()) << "the step in flight keeps the owner's code";
    std::string refusal;
    EXPECT_FALSE(f.Runs.Control(id, R::StepRunAction::Accept, 1u, refusal));
    EXPECT_FALSE(f.Runs.Control(id, R::StepRunAction::Run, 1u, refusal));

    {
        std::lock_guard lock(mutex);
        release = true;
    }
    wake.notify_all();
    ASSERT_TRUE(f.Jobs.FramesUntil([&] { return probe.Alive.expired(); })) << "dropped after the late completion";
    EXPECT_TRUE(clockAlive.expired()) << "the owner's clock too";
    EXPECT_EQ(probe.Discards, 1);
    EXPECT_EQ(probe.Commits, 0);
    EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Discarded);
    EXPECT_FALSE(f.Runs.Control(id, R::StepRunAction::Accept, 1u, refusal)) << refusal;
    EXPECT_FALSE(f.Runs.Control(id, R::StepRunAction::Run, 1u, refusal));
}

TEST(StepRuns, AcceptCommitsOnceAndOnlyFromAStoppedRun)
{
    auto problem = std::make_shared<Problem>();
    Probe probe;
    Fixture f;
    const auto id = f.Start(problem, probe);
    problem.reset();
    std::string refusal;
    EXPECT_FALSE(f.Runs.Control(id, R::StepRunAction::Accept, 1u, refusal)) << "no step yet";
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, 3u));
    ASSERT_TRUE(f.Settle(id));
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Accept));
    EXPECT_FALSE(f.Runs.Control(id, R::StepRunAction::Accept, 1u, refusal)) << "already requested";
    ASSERT_TRUE(f.Jobs.FramesUntil([&] { return f.Observe(id).Phase == R::StepRunPhase::Committed; }));
    for (int i = 0; i < 5; ++i) f.Jobs.Frame();
    EXPECT_EQ(probe.Commits, 1);
    EXPECT_EQ(probe.Discards, 0) << "a committed run discards nothing";
    EXPECT_TRUE(probe.Alive.expired());
    EXPECT_FALSE(f.Runs.Control(id, R::StepRunAction::Accept, 1u, refusal));
    EXPECT_FALSE(f.Runs.Control(id, R::StepRunAction::Discard, 1u, refusal));
    EXPECT_EQ(probe.Commits, 1);
}

TEST(StepRuns, OnlyAConvergedRunCommitsByItselfAndOnlyWhenAsked)
{
    const auto ended = [](R::StepRunOptions options, const bool stop)
    {
        auto problem = std::make_shared<Problem>();
        Probe probe;
        Fixture f;
        const auto id = f.Start(problem, probe, std::move(options));
        EXPECT_TRUE(f.Control(id, R::StepRunAction::Step, 4u));
        EXPECT_TRUE(f.Settle(id));
        if (stop) EXPECT_TRUE(f.Control(id, R::StepRunAction::Stop));
        else EXPECT_TRUE(f.Control(id, R::StepRunAction::Run));
        EXPECT_TRUE(f.Settle(id));
        for (int i = 0; i < 10; ++i) f.Jobs.Frame();
        return std::pair{f.Observe(id), probe.Commits};
    };
    auto [converged, commits] = ended({.AcceptOnConvergence = true}, false);
    EXPECT_EQ(converged.Phase, R::StepRunPhase::Committed);
    EXPECT_EQ(commits, 1);
    std::tie(converged, commits) = ended({}, false);
    EXPECT_EQ(converged.Phase, R::StepRunPhase::Paused);
    EXPECT_EQ(converged.End, R::StepRunEnd::Converged);
    EXPECT_EQ(commits, 0) << "without accept-on-convergence it waits";
    std::tie(converged, commits) = ended({.IterationCap = 6u, .AcceptOnConvergence = true}, false);
    EXPECT_EQ(converged.End, R::StepRunEnd::IterationCap);
    EXPECT_EQ(converged.Phase, R::StepRunPhase::Paused);
    EXPECT_EQ(converged.Steps, 6u);
    EXPECT_EQ(commits, 0) << "the cap is not convergence";
    std::tie(converged, commits) = ended({.AcceptOnConvergence = true}, true);
    EXPECT_EQ(converged.End, R::StepRunEnd::Stopped);
    EXPECT_EQ(converged.Phase, R::StepRunPhase::Paused);
    EXPECT_EQ(commits, 0) << "Stop is not convergence";
}

TEST(StepRuns, ARefusedCommitWithValidCapturesEndsTheRunFailed)
{
    auto problem = std::make_shared<Problem>();
    Probe probe;
    Fixture f;
    probe.CommitResult = false;
    const auto id = f.Start(problem, probe);
    problem.reset();
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, 2u));
    ASSERT_TRUE(f.Settle(id));
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Accept));
    ASSERT_TRUE(f.Jobs.FramesUntil([&] { return R::IsTerminal(f.Observe(id).Phase); }));
    for (int i = 0; i < 5; ++i) f.Jobs.Frame();
    EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Failed);
    EXPECT_EQ(f.Observe(id).Diagnostic, "stale: refused by the owner");
    EXPECT_EQ(probe.Commits, 1);
    EXPECT_EQ(probe.Discards, 1) << "the refused run releases what it shows";
    EXPECT_TRUE(probe.Alive.expired()) << "and drops the owner's code";
    std::string refusal;
    for (const auto action : {R::StepRunAction::Run, R::StepRunAction::Step, R::StepRunAction::Accept})
        EXPECT_FALSE(f.Runs.Control(id, action, 1u, refusal));
}

TEST(StepRuns, ARestartReplacesThePendingOneAndOlderPreviewsAreNotShown)
{
    int inits = 0;
    bool olderCalled = false;
    std::mutex mutex;
    std::condition_variable wake;
    bool inside = false, release = false;
    auto problem = std::make_shared<Problem>();
    Probe probe;
    Fixture f;
    problem->InsideStep = [&]
    {
        std::unique_lock lock(mutex);
        if (release) return;
        inside = true;
        wake.notify_all();
        wake.wait_for(lock, 10s, [&] { return release; });
    };
    const auto id = f.Start(problem, probe, {.PreviewInterval = std::chrono::nanoseconds{0}});
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
    ASSERT_TRUE(f.Jobs.FramesUntil([&] { std::lock_guard lock(mutex); return inside; }));
    std::string refusal;
    const std::size_t shownBefore = probe.ShownGenerations.size();
    ASSERT_TRUE(f.Runs.Restart(id, [&](std::string&) { olderCalled = true; return true; }, refusal));
    ASSERT_TRUE(f.Runs.Restart(id, [&, problem](std::string&) { ++inits; problem->X = 10.0; return true; }, refusal));
    {
        std::lock_guard lock(mutex);
        release = true;
    }
    wake.notify_all();
    ASSERT_TRUE(f.Jobs.FramesUntil([&] { return f.Observe(id).Generation == 1u; }));
    ASSERT_TRUE(f.Settle(id));
    for (int i = 0; i < 5; ++i) f.Jobs.Frame();
    EXPECT_FALSE(olderCalled) << "the replaced restart never ran";
    EXPECT_EQ(inits, 1);
    EXPECT_EQ(f.Observe(id).End, R::StepRunEnd::Converged) << "the newest computation ran";
    EXPECT_EQ(f.Observe(id).Metrics->Records().front().Values[0], 6.0) << "from x = 10";
    ASSERT_GT(probe.ShownGenerations.size(), shownBefore);
    for (std::size_t i = shownBefore; i < probe.ShownGenerations.size(); ++i)
        EXPECT_EQ(probe.ShownGenerations[i], 1u) << "after the request only the newest computation shows";
}

TEST(StepRuns, InvalidCapturesEndTheRunWhilePausedAndWhileComputing)
{
    for (const bool computing : {false, true})
    {
        std::atomic<bool> release{false};
        auto problem = std::make_shared<Problem>();
        Probe probe;
        Fixture f;
        problem->InsideStep = [&] { while (computing && !release) std::this_thread::sleep_for(100us); };
        const auto id = f.Start(problem, probe);
        problem.reset();
        ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, computing ? 50u : 2u));
        if (computing)
            ASSERT_TRUE(f.Jobs.FramesUntil([&] { return f.Observe(id).SegmentInFlight; }));
        else
            ASSERT_TRUE(f.Settle(id));
        const int shows = probe.Shows;
        probe.Valid = false; // e.g. another world became active
        f.Jobs.Frame();
        EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Discarded) << "computing " << computing;
        EXPECT_NE(f.Observe(id).Diagnostic.find("stale"), std::string::npos);
        EXPECT_EQ(probe.Discards, 1);
        release = true;
        ASSERT_TRUE(f.Jobs.FramesUntil([&] { return probe.Alive.expired(); }));
        EXPECT_EQ(probe.Shows, shows) << "nothing shown after";
        EXPECT_EQ(probe.Commits, 0);
    }
}

TEST(StepRuns, BoundedDrainsWithAnotherParkedJobStillComplete)
{
    const auto reference = Uninterrupted(); // before this fixture: each fixture owns the scheduler
    auto problem = std::make_shared<Problem>();
    Probe probe;
    Fixture f;
    f.Jobs.ApplyBudget = 1u;
    f.Jobs.GateBudget = 1u;
    R::PluginJob parked{.Label = "parked"};
    parked.Work = [](const std::function<bool()>&) {};
    parked.IsReadyToApply = [] { return false; };
    parked.Completion = [](bool) { return true; };
    ASSERT_TRUE(f.Jobs.Submit(std::move(parked)).IsValid());
    const auto id = f.Start(problem, probe);
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, 3u));
    ASSERT_TRUE(f.Settle(id));
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
    ASSERT_TRUE(f.Settle(id));
    EXPECT_EQ(f.Trace(id), reference);
    EXPECT_EQ(problem->MaxActive.load(), 1) << "no duplicate segment";
    EXPECT_EQ(f.Jobs.Submitted, 1 + 1 + 2) << "the parked job, the holder and one segment per control";
}

TEST(StepRuns, McpControlsAndObservesARun)
{
    auto problem = std::make_shared<Problem>();
    Probe probe;
    Fixture f;
    R::AgentOperationRegistry registry;
    f.Runs.RegisterOperations(registry);
    const auto id = f.Start(problem, probe);
    problem.reset();
    const auto call = [&](const char* name, const Json& args) // as the agent server dispatches: schema first
    { return R::InvokeAgentOperation(registry, name, {}, args.dump(), false); };

    auto out = call("run_control", {{"run", id}, {"action", "step"}, {"count", 4}});
    ASSERT_FALSE(out.IsError) << out.Text;
    ASSERT_TRUE(f.Settle(id));
    out = call("run_observe", {{"run", id}, {"metrics", true}});
    ASSERT_FALSE(out.IsError) << out.Text;
    auto observed = Json::parse(out.Text);
    EXPECT_EQ(observed["phase"], "paused");
    EXPECT_EQ(observed["steps"], 4);
    EXPECT_EQ(observed["metrics"].size(), 4u);
    EXPECT_EQ(observed["last_metric"]["values"]["x"], f.Observe(id).Metrics->Records().back().Values[0]);
    EXPECT_EQ(Json::parse(call("run_observe", Json::object()).Text)["runs"], Json::array({id}));

    ASSERT_FALSE(call("run_control", {{"run", id}, {"action", "stop"}}).IsError);
    out = call("run_control", {{"run", id}, {"action", "run"}});
    EXPECT_TRUE(out.IsError) << "a stopped run does not resume";
    EXPECT_EQ(out.ErrorCode, "refused");
    ASSERT_FALSE(call("run_control", {{"run", id}, {"action", "discard"}}).IsError);
    ASSERT_TRUE(f.Jobs.FramesUntil([&] { return probe.Alive.expired(); }));
    EXPECT_EQ(Json::parse(call("run_observe", {{"run", id}}).Text)["phase"], "discarded");
    EXPECT_EQ(probe.Discards, 1);
    EXPECT_TRUE(call("run_control", {{"run", 999}, {"action", "run"}}).IsError);
    out = call("run_control", {{"run", id}, {"action", "step"}, {"count", 4294967297ull}});
    EXPECT_EQ(out.ErrorCode, "invalid_params") << "not narrowed to one step";
}

TEST(StepRuns, AnOlderComputationsConvergenceNeverAcceptsItsRestart)
{
    std::mutex mutex;
    std::condition_variable wake;
    bool inside = false, release = false;
    auto problem = std::make_shared<Problem>();
    problem->Tolerance = 2.0; // the first step converges
    Probe probe;
    Fixture f;
    problem->InsideStep = [&]
    {
        std::unique_lock lock(mutex);
        if (release) return;
        inside = true;
        wake.notify_all();
        wake.wait_for(lock, 10s, [&] { return release; });
    };
    const auto id = f.Start(problem, probe, {.AcceptOnConvergence = true});
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
    ASSERT_TRUE(f.Jobs.FramesUntil([&] { std::lock_guard lock(mutex); return inside; }));
    std::string refusal;
    ASSERT_TRUE(f.Runs.Restart(id, [problem](std::string&) { problem->X = 10.0; problem->Tolerance = 1.0e-9; return true; },
                               refusal));
    {
        std::lock_guard lock(mutex);
        release = true; // the older computation's step converges now
    }
    wake.notify_all();
    ASSERT_TRUE(f.Jobs.FramesUntil([&] { return R::IsTerminal(f.Observe(id).Phase); }));
    const auto run = f.Observe(id);
    EXPECT_EQ(run.Phase, R::StepRunPhase::Committed);
    EXPECT_EQ(run.Generation, 1u);
    EXPECT_GT(run.Steps, 1u) << "the restart computed to its own convergence before it was accepted";
    EXPECT_EQ(probe.Commits, 1);
}

TEST(StepRuns, OnlyTheNewest32RetiredRunsStayObservable)
{
    std::vector<std::shared_ptr<Problem>> problems;
    Probe probe;
    Fixture f;
    std::vector<std::uint64_t> ids;
    for (int i = 0; i < 40; ++i)
    {
        problems.push_back(std::make_shared<Problem>());
        ids.push_back(f.Start(problems.back(), probe));
    }
    for (const auto id : ids) ASSERT_TRUE(f.Control(id, R::StepRunAction::Discard));
    for (int i = 0; i < 10; ++i) f.Jobs.Frame(); // every run retires; no further start
    const auto kept = f.Runs.Runs();
    ASSERT_EQ(kept.size(), 32u);
    EXPECT_EQ(kept.front(), ids[8]) << "the oldest go first";
    EXPECT_FALSE(f.Runs.Observe(ids[0]));
    EXPECT_EQ(probe.Discards, 40);
}

TEST(StepRuns, ACopyShowRefusedIsOfferedAgainUntilShown)
{
    auto problem = std::make_shared<Problem>();
    Probe probe;
    probe.ShowRefusals = 3; // e.g. both preview slots busy
    Fixture f;
    const auto id = f.Start(problem, probe, {.PreviewInterval = std::chrono::nanoseconds{0}});
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, 1u)); // one copy, no newer one to replace it
    ASSERT_TRUE(f.Settle(id));
    ASSERT_TRUE(f.Jobs.FramesUntil([&]
    {
        if (probe.Shows == 0) EXPECT_FALSE(f.Observe(id).ShownStep) << "not shown while refused";
        return probe.Shows == 1;
    })) << "the paused state shows in the end";
    EXPECT_EQ(probe.ShowAttempts, 4);
    EXPECT_EQ(f.Observe(id).PreviewCopies, 1u);
    EXPECT_EQ(f.Observe(id).ShownStep, 1u) << "counted as shown only once it was";
    for (int i = 0; i < 5; ++i) f.Jobs.Frame();
    EXPECT_EQ(probe.ShowAttempts, 4) << "once shown, not offered again";
}

TEST(StepRuns, ACopyThatCannotBeShownIsNeitherRetriedNorCountedAsShown)
{
    auto problem = std::make_shared<Problem>();
    Probe probe;
    probe.ShowNever = true; // e.g. no operational device
    Fixture f;
    const auto id = f.Start(problem, probe, {.PreviewInterval = std::chrono::nanoseconds{0}});
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, 1u));
    ASSERT_TRUE(f.Settle(id));
    for (int i = 0; i < 10; ++i) f.Jobs.Frame();
    EXPECT_EQ(probe.ShowAttempts, 1) << "offered once, not again";
    EXPECT_FALSE(f.Observe(id).ShownStep) << "nothing was shown";
}
