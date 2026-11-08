// ARCH-022 S3: the step-run driver with a deterministic step, the real job service and a test owner that carries the
// run's jobs as the plugin host does (joined jobs, a run cancel cancels them all, a refused publication is not
// finalised twice). Each test fails without the mechanism it names.
#include <algorithm>
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
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include "TestImGuiFrameScope.hpp"

import Extrinsic.Core.Tasks;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.Runtime.StepRunControls;

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
        std::function<R::JobApplyValidation()> Validate{}; // the job service's final check after a job's gate
        // ARCH-023: an owner whose surface cannot cancel (a detached session's), and one output for every run: a
        // holder is refused while an earlier holder is not yet finished (the plugin host's and editor's admission).
        bool RefuseCancel{false}, OneOutput{false};
        std::vector<R::JobToken> Holders{};

        [[nodiscard]] bool Active(const R::JobToken token) const
        {
            const auto state = Jobs.GetState(token);
            return state == R::JobState::AwaitingDependencies || state == R::JobState::Queued ||
                state == R::JobState::Running || state == R::JobState::AwaitingGate ||
                state == R::JobState::AwaitingApply;
        }
        [[nodiscard]] bool OutputBusy() const
        {
            return std::ranges::any_of(Holders,
                [this](const R::JobToken token)
                {
                    return Active(token);
                });
        }

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
            const bool holder = !job.Run.IsValid();
            if (holder && OneOutput && OutputBusy())
                return {};
            auto held = std::make_shared<Held>(Held{.Job = std::move(job)});
            R::JobDesc desc{};
            desc.DebugName = held->Job.Label;
            desc.Work = [held](const R::JobCancellation& cancellation)
            {
                held->Job.Work([&cancellation] { return cancellation.IsCancelled(); });
                return R::JobResultEnvelope::Make(true);
            };
            if (held->Job.IsReadyToApply) desc.IsReadyToApply = [held] { return held->Job.IsReadyToApply(); };
            if (Validate) desc.ValidateBeforeApply = [validate = Validate] { return validate(); };
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
            if (holder)
                Holders.push_back(token);
            return {token};
        }

        bool Cancel(const R::PluginJobHandle job)
        {
            if (RefuseCancel)
                return false;
            bool any = false;
            for (const auto token : RunJobs[job.Token]) any |= Jobs.Cancel(token);
            return any;
        }

        std::function<void()> BeforeFrame{}; // an owner-driven run's owner, ticking in its own maintenance

        void Frame()
        {
            if (BeforeFrame) BeforeFrame();
            (void)Jobs.DrainCompletions(Events, ApplyBudget, GateBudget);
        }

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
            .Step =
                [problem](R::StepRunMetric& metric)
            {
                const int active = ++problem->Active;
                int seen = problem->MaxActive.load();
                while (active > seen && !problem->MaxActive.compare_exchange_weak(seen, active))
                {
                }
                if (problem->InsideStep)
                    problem->InsideStep();
                const double next = problem->X * 0.5 + 1.0;
                const double change = std::abs(next - problem->X);
                problem->X = next;
                metric.Values = {next, change, 0.0, 0.0};
                --problem->Active;
                return change < problem->Tolerance ? R::StepOutcome::Converged : R::StepOutcome::Continue;
            },
            .Preview =
                [problem](R::StepPreview& preview)
            {
                preview.Values.assign(1, static_cast<float>(problem->X));
                return true;
            },
            .Show =
                [&probe](const R::StepPreview& preview)
            {
                ++probe.ShowAttempts;
                if (probe.ShowNever)
                    return R::StepRunShow::Never;
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
            .Valid =
                [&probe]
            {
                return probe.Valid;
            },
            .Commit =
                [&probe](std::string& diagnostic)
            {
                ++probe.Commits;
                if (!probe.CommitResult)
                    diagnostic = "stale: refused by the owner";
                return probe.CommitResult ? R::StepRunCommit::Committed : R::StepRunCommit::Refused;
            },
            .Discard =
                [&probe]
            {
                ++probe.Discards;
            },
        };
    }

    struct Fixture
    {
        SchedulerScope Scheduler{};
        Owner Jobs{};
        R::StepRunsService Runs{};

        std::uint64_t Start(const std::shared_ptr<Problem>& problem,
            Probe& probe,
            R::StepRunOptions options = {},
            const bool preview = true,
            std::function<bool(std::string&)> restart = {})
        {
            if (options.Label.empty()) options.Label = "fixed point";
            options.Owner = "test";
            options.MetricNames = {"x", "change", "", ""};
            auto callbacks = Callbacks(problem, probe);
            if (!preview) callbacks.Preview = {};
            callbacks.Restart = std::move(restart);
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

// ARCH-023: how an owner's result names a run that ended without a commit (the plugins' Discard callables use it).
TEST(StepRuns, UncommittedStatusNamesHowARunEndedWithoutACommit)
{
    const auto status = [](const R::StepRunPhase phase, std::string diagnostic)
    {
        return std::string{R::UncommittedStatus({.Diagnostic = std::move(diagnostic), .Phase = phase})};
    };
    EXPECT_EQ(status(R::StepRunPhase::Discarded, "discarded"), "discarded");
    EXPECT_EQ(status(R::StepRunPhase::Discarded, "cancelled"), "cancelled");
    EXPECT_EQ(status(R::StepRunPhase::Discarded, "its owner shut down"), "cancelled");
    EXPECT_EQ(status(R::StepRunPhase::Discarded, "stale: the captured inputs or the world changed"), "stale");
    EXPECT_EQ(status(R::StepRunPhase::Failed, "stale: the captured inputs changed"), "stale")
        << "a Commit refused at the holder's final check";
    EXPECT_EQ(status(R::StepRunPhase::Failed, "a step failed"), "failed");
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
    const auto id = f.Start(problem, probe, {.Execution = {.PreviewIntervalMs = 0u}});
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
    auto [converged, commits] = ended({.Execution = {.AutoAccept = R::StepRunAutoAccept::Converged}}, false);
    EXPECT_EQ(converged.Phase, R::StepRunPhase::Committed);
    EXPECT_EQ(commits, 1);
    std::tie(converged, commits) = ended({}, false);
    EXPECT_EQ(converged.Phase, R::StepRunPhase::Paused);
    EXPECT_EQ(converged.End, R::StepRunEnd::Converged);
    EXPECT_EQ(commits, 0) << "without accept-on-convergence it waits";
    std::tie(converged, commits) =
        ended({.Execution = {.IterationCap = 6u, .AutoAccept = R::StepRunAutoAccept::Converged}}, false);
    EXPECT_EQ(converged.End, R::StepRunEnd::IterationCap);
    EXPECT_EQ(converged.Phase, R::StepRunPhase::Paused);
    EXPECT_EQ(converged.Steps, 6u);
    EXPECT_EQ(commits, 0) << "the cap is not convergence";
    std::tie(converged, commits) = ended({.Execution = {.AutoAccept = R::StepRunAutoAccept::Converged}}, true);
    EXPECT_EQ(converged.End, R::StepRunEnd::Stopped);
    EXPECT_EQ(converged.Phase, R::StepRunPhase::Paused);
    EXPECT_EQ(commits, 0) << "Stop is not convergence";
}

TEST(StepRuns, AWorkerRunCommitsOnlyAfterTheHoldersFinalChecks)
{
    auto problem = std::make_shared<Problem>();
    Probe probe;
    Fixture f;
    bool stale = false;
    f.Jobs.Validate = [&stale] { return stale ? R::JobApplyValidation::StaleWorld : R::JobApplyValidation::Current; };
    const auto id = f.Start(problem, probe);
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, 2u));
    ASSERT_TRUE(f.Settle(id));
    stale = true; // the world changed: the job service refuses the holder after its gate
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Accept));
    ASSERT_TRUE(f.Jobs.FramesUntil([&] { return R::IsTerminal(f.Observe(id).Phase); }));
    EXPECT_EQ(probe.Commits, 0) << "nothing was written";
    EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Discarded);
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
    const auto id = f.Start(problem, probe, {.Execution = {.PreviewIntervalMs = 0u}});
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
    const auto id = f.Start(problem, probe, {.Execution = {.AutoAccept = R::StepRunAutoAccept::Converged}});
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
    // Every run retires once its holder job's gate runs (after a worker ran its work); no further start.
    ASSERT_TRUE(f.Jobs.FramesUntil([&] { return probe.Discards == 40; }));
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
    const auto id = f.Start(problem, probe, {.Execution = {.PreviewIntervalMs = 0u}});
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
    const auto id = f.Start(problem, probe, {.Execution = {.PreviewIntervalMs = 0u}});
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, 1u));
    ASSERT_TRUE(f.Settle(id));
    for (int i = 0; i < 10; ++i) f.Jobs.Frame();
    EXPECT_EQ(probe.ShowAttempts, 1) << "offered once, not again";
    EXPECT_FALSE(f.Observe(id).ShownStep) << "nothing was shown";
}

namespace
{
    // An owner-driven run's owner, as a resident GPU method: each frame it consumes its submitted page (one iteration
    // of the fixed point), reports it, and records the next one at once while the run allows it; a test holds a page
    // or forces an iteration's outcome. Its callables count what the run asked of it.
    struct GpuOwner
    {
        R::StepRunDriver Driver{};
        std::shared_ptr<Problem> Solve{std::make_shared<Problem>()};
        bool InFlight{false}, Hold{false};
        std::optional<R::StepOutcome> Force{};
        bool AskPreview{false}; // asks MayPreview after each iteration that goes on
        std::vector<std::uint32_t> Previewed{}; // the iterations it was allowed a preview after
        int Commits{0}, Discards{0};
        R::StepRunCommit Answer{R::StepRunCommit::Committed};
        bool Valid{true};
        std::uint32_t Iterations{0u};
        std::shared_ptr<int> Token{std::make_shared<int>(0)}; // held by the run's callables only (after Start)
        std::weak_ptr<int> Alive{};

        void Frame()
        {
            if (InFlight)
            {
                if (Hold) return;
                const double next = Solve->X * 0.5 + 1.0, change = std::abs(next - Solve->X);
                Solve->X = next;
                const auto outcome = Force.value_or(change < Solve->Tolerance ? R::StepOutcome::Converged : R::StepOutcome::Continue);
                const bool more = Driver.Stepped({.Values = {next, change, 0.0, 0.0}}, outcome);
                if (more && AskPreview && Driver.MayPreview())
                {
                    Driver.Previewed(); // the owner's copy is submitted (MayPreview alone counts nothing)
                    Previewed.push_back(++Iterations);
                }
                else
                    ++Iterations;
                InFlight = more; // the next page is recorded at once
                if (!more) Driver.Recording(false);
                return;
            }
            if (Driver.MayStart())
            {
                InFlight = true;
                Driver.Recording(true);
            }
        }

        R::StepRunCallbacks Callbacks()
        {
            Alive = Token;
            return {.Valid = [this] { return Valid; },
                    .Commit = [this, keep = Token](std::string&) { ++Commits; return Answer; },
                    .Discard = [this] { ++Discards; }};
        }
    };

    struct GpuFixture
    {
        GpuOwner Gpu{};
        Fixture F{};

        std::uint64_t Start(R::StepRunOptions options = {})
        {
            options.Label = "resident";
            options.Owner = "test";
            std::string refusal;
            const auto id = F.Runs.Start(std::move(options), Gpu.Callbacks(), F.Jobs.Bind(), refusal);
            EXPECT_NE(id, 0u) << refusal;
            Gpu.Driver = F.Runs.Driver(id);
            Gpu.Token.reset(); // the run's Commit callable holds it now
            F.Jobs.BeforeFrame = [this] { Gpu.Frame(); };
            return id;
        }
        R::StepRunObservation Observe(const std::uint64_t id) { return *F.Runs.Observe(id); }
        bool Control(const std::uint64_t id, const R::StepRunAction action, const std::uint32_t count = 1u)
        {
            return F.Control(id, action, count);
        }
        bool Until(const std::function<bool()>& done) { return F.Jobs.FramesUntil(done); }
        void Frames(const int n) { for (int i = 0; i < n; ++i) F.Jobs.Frame(); }
    };
}

TEST(StepRuns, AnOwnerDrivenRunCountsItsIterationsAndPausesOnlyAtABoundary)
{
    GpuFixture t;
    auto& gpu = t.Gpu;
    const auto id = t.Start();
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Step, 3u));
    ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == R::StepRunPhase::Paused; }));
    EXPECT_EQ(t.Observe(id).Steps, 3u);
    const auto records = t.Observe(id).Metrics->Records();
    ASSERT_EQ(records.size(), 3u) << "every iteration is logged";
    EXPECT_EQ(records[2].Step, 3u);
    EXPECT_EQ(records[2].Values[0], 1.75) << "x after three iterations of x/2 + 1 from 0";
    EXPECT_FALSE(gpu.Driver.MayStart()) << "paused";
    t.Frames(10);
    EXPECT_EQ(t.Observe(id).Steps, 3u) << "nothing recorded while paused";
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Run));
    ASSERT_TRUE(t.Until([&] { return t.Observe(id).Steps >= 5u; }));
    gpu.Hold = true; // a page is held
    ASSERT_TRUE(t.Until([&] { return gpu.InFlight; }));
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Pause));
    t.Frames(5);
    EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Running) << "a pause settles only once the page is consumed";
    EXPECT_TRUE(t.Observe(id).SegmentInFlight);
    std::string refusal;
    EXPECT_FALSE(t.F.Runs.Control(id, R::StepRunAction::Accept, 1u, refusal)) << "not while a page is held";
    const auto held = t.Observe(id).Steps;
    gpu.Hold = false;
    ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == R::StepRunPhase::Paused; }));
    EXPECT_EQ(t.Observe(id).Steps, held + 1u) << "the held page counts, no further one";
    EXPECT_TRUE(t.Control(id, R::StepRunAction::Accept));
    ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == R::StepRunPhase::Committed; }));
    EXPECT_EQ(gpu.Commits, 1);
}

// ARCH-023: an owner that batches iterations into one submission (FPS chunks, smoothing passes) asks its allowance
// and reports a batch at once; every iteration of a batch counts, a pause or stop during it included.
TEST(StepRuns, AnOwnersAllowanceIsWhatIsLeftOfStepNAndTheCapAndABatchCountsWhole)
{
    GpuFixture t;
    auto& driver = t.Gpu.Driver;
    const auto id = t.Start({.Execution = {.IterationCap = 10u}});
    t.F.Jobs.BeforeFrame = {}; // the test is the owner
    EXPECT_EQ(driver.Allowance(), 0u) << "ready: nothing may be recorded";
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Step, 4u));
    EXPECT_EQ(driver.Allowance(), 4u) << "step 4";
    driver.Recording(true);
    EXPECT_TRUE(driver.Stepped({.Values = {1.0, 0.0, 0.0, 0.0}}, R::StepOutcome::Continue, 3u));
    EXPECT_EQ(t.Observe(id).Steps, 3u);
    EXPECT_EQ(driver.Allowance(), 1u) << "one step of the four is left";
    EXPECT_FALSE(driver.Stepped({.Values = {2.0, 0.0, 0.0, 0.0}}, R::StepOutcome::Continue, 1u)) << "step 4 is spent";
    driver.Recording(false);
    EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Paused);
    EXPECT_EQ(driver.Allowance(), 0u);
    const auto records = t.Observe(id).Metrics->Records();
    ASSERT_EQ(records.size(), 2u) << "one record per reported batch";
    EXPECT_EQ(records[0].Step, 3u) << "a batch's record carries its last step";
    EXPECT_EQ(records[1].Step, 4u);
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Run));
    EXPECT_EQ(driver.Allowance(), 6u) << "run: the cap's remainder";
    driver.Recording(true);
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Pause));
    EXPECT_FALSE(driver.Stepped({}, R::StepOutcome::Continue, 6u)) << "a batch in flight completes after the pause";
    EXPECT_EQ(t.Observe(id).Steps, 10u) << "every iteration of the batch counts";
    EXPECT_EQ(t.Observe(id).End, R::StepRunEnd::IterationCap);
    driver.Recording(false);
    EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Paused);
}

TEST(StepRuns, TheMetricLogIsBoundedWhileEveryStepCounts)
{
    GpuFixture t;
    auto& driver = t.Gpu.Driver;
    constexpr std::uint32_t kSteps = R::kMaxStepRunMetricRecords + 3u;
    const auto id = t.Start({.Execution = {.IterationCap = kSteps}});
    t.F.Jobs.BeforeFrame = {};
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Run));
    driver.Recording(true);
    for (std::uint32_t i = 0; i < kSteps; ++i)
        (void)driver.Stepped({.Values = {double(i), 0.0, 0.0, 0.0}}, R::StepOutcome::Continue);
    driver.Recording(false);
    const auto observed = t.Observe(id);
    EXPECT_EQ(observed.Steps, kSteps);
    EXPECT_EQ(observed.End, R::StepRunEnd::IterationCap);
    ASSERT_EQ(observed.Metrics->Records().size(), R::kMaxStepRunMetricRecords) << "the first records are kept";
    EXPECT_EQ(observed.Metrics->Records().back().Step, R::kMaxStepRunMetricRecords);
}

TEST(StepRuns, APreviewAtABatchEndFollowsTheCadenceAndTheSwitch)
{
    {
        GpuFixture t;
        const auto id = t.Start({.Execution = {.PreviewEvery = 4u, .PreviewIntervalMs = 0u}});
        t.F.Jobs.BeforeFrame = {};
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Run));
        EXPECT_TRUE(t.Gpu.Driver.MayPreviewAt(8u));
        EXPECT_FALSE(t.Gpu.Driver.MayPreviewAt(7u)) << "off the cadence";
    }
    GpuFixture t;
    const auto id = t.Start({.Execution = {.PreviewEvery = 0u}});
    t.F.Jobs.BeforeFrame = {};
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Run));
    EXPECT_FALSE(t.Gpu.Driver.MayPreviewAt(1u)) << "live preview off";
}

TEST(StepRuns, StopStaysAgainstALateConvergenceAndConvergenceWinsOverTheCap)
{
    {   // Stop while a converging page is held
        GpuFixture t;
        const auto id = t.Start({.Execution = {.AutoAccept = R::StepRunAutoAccept::End}});
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Run));
        ASSERT_TRUE(t.Until([&] { return t.Observe(id).Steps >= 2u; }));
        t.Gpu.Hold = true;
        ASSERT_TRUE(t.Until([&] { return t.Gpu.InFlight; }));
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Stop));
        t.Gpu.Force = R::StepOutcome::Converged;
        t.Gpu.Hold = false;
        ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == R::StepRunPhase::Paused; }));
        t.Frames(5);
        EXPECT_EQ(t.Observe(id).End, R::StepRunEnd::Stopped) << "the earlier Stop stays";
        EXPECT_EQ(t.Gpu.Commits, 0) << "a stopped run is never accepted on its own";
    }
    for (const bool onEnd : {false, true})
    {   // convergence on the iteration that also reaches the cap
        GpuFixture t;
        t.Gpu.Force = R::StepOutcome::Converged;
        const auto id =
            t.Start({.Execution = {.IterationCap = 1u,
                         .AutoAccept = onEnd ? R::StepRunAutoAccept::End : R::StepRunAutoAccept::Converged}});
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Run));
        ASSERT_TRUE(t.Until([&] { return R::IsTerminal(t.Observe(id).Phase); }));
        EXPECT_EQ(t.Observe(id).End, R::StepRunEnd::Converged) << "convergence wins over the simultaneous cap";
        EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Committed);
    }
}

TEST(StepRuns, AcceptOnEndAcceptsACappedBatchAndOnlyThat)
{
    for (const bool onEnd : {false, true})
    {
        GpuFixture t;
        t.Gpu.Solve->Tolerance = 0.0; // never converges
        const auto id = t.Start({.Execution = {.IterationCap = 4u,
                                     .AutoAccept = onEnd ? R::StepRunAutoAccept::End : R::StepRunAutoAccept::Never}});
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Run));
        ASSERT_TRUE(t.Until([&] { return t.Observe(id).End != R::StepRunEnd::None && !t.Observe(id).SegmentInFlight; }));
        const auto settled = onEnd ? R::StepRunPhase::Committed : R::StepRunPhase::Paused;
        ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == settled; })) << "the pump's effect, not a frame count";
        t.Frames(5);
        EXPECT_EQ(t.Observe(id).End, R::StepRunEnd::IterationCap);
        EXPECT_EQ(t.Observe(id).Phase, settled);
        EXPECT_EQ(t.Gpu.Commits, onEnd ? 1 : 0);
    }
}

TEST(StepRuns, APendingCommitDecidesAfterItsPublicationStartedAndNotBefore)
{
    {   // pending, the captures change meanwhile, then committed: not discarded as stale
        GpuFixture t;
        t.Gpu.Answer = R::StepRunCommit::Pending;
        const auto id = t.Start();
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Step, 2u));
        ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == R::StepRunPhase::Paused; }));
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Accept));
        ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == R::StepRunPhase::Accepting; }));
        t.Gpu.Valid = false; // the publication changes what was captured
        t.Frames(5);
        EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Accepting);
        t.Gpu.Driver.Publishing();
        t.Gpu.Driver.CommitFinished(true);
        ASSERT_TRUE(t.Until([&] { return R::IsTerminal(t.Observe(id).Phase); }));
        EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Committed);
        EXPECT_EQ(t.Gpu.Commits, 1);
        EXPECT_EQ(t.Gpu.Discards, 0);
    }
    {   // pending, discarded before its publication: the discard wins, a late answer counts for nothing
        GpuFixture t;
        t.Gpu.Answer = R::StepRunCommit::Pending;
        const auto id = t.Start();
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Step, 2u));
        ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == R::StepRunPhase::Paused; }));
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Accept));
        ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == R::StepRunPhase::Accepting; }));
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Discard));
        ASSERT_TRUE(t.Until([&] { return t.Gpu.Discards == 1; })) << "the owner abandons its readback";
        t.Gpu.Driver.CommitFinished(true);
        ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == R::StepRunPhase::Discarded; }));
        t.Frames(5);
        EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Discarded);
        EXPECT_EQ(t.Gpu.Discards, 1);
    }
    {   // publishing: a discard is refused and a cancel of the holder waits for the publication's end
        GpuFixture t;
        t.Gpu.Answer = R::StepRunCommit::Pending;
        const auto id = t.Start();
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Step, 2u));
        ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == R::StepRunPhase::Paused; }));
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Accept));
        ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == R::StepRunPhase::Accepting; }));
        t.Gpu.Driver.Publishing();
        std::string refusal;
        EXPECT_FALSE(t.F.Runs.Control(id, R::StepRunAction::Discard, 1u, refusal)) << "the publication decides";
        ASSERT_TRUE(t.F.Jobs.Cancel({t.F.Jobs.RunJobs.begin()->second.front()}));
        t.Frames(5);
        EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Accepting) << "the holder finished, the publication goes on";
        t.Gpu.Driver.CommitFinished(true);
        EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Committed) << "settled by the answer itself";
        EXPECT_EQ(t.Gpu.Discards, 0);
    }
    for (const bool committed : {true, false})
    {   // answered, then the holder is cancelled before its gate is asked again: the answer stands
        GpuFixture t;
        t.Gpu.Answer = R::StepRunCommit::Pending;
        const auto id = t.Start();
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Step, 2u));
        ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == R::StepRunPhase::Paused; }));
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Accept));
        ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == R::StepRunPhase::Accepting; }));
        t.Gpu.Driver.Publishing();
        t.Gpu.Driver.CommitFinished(committed, committed ? "" : "stale: the output changed");
        ASSERT_TRUE(t.F.Jobs.Cancel({t.F.Jobs.RunJobs.begin()->second.front()}));
        ASSERT_TRUE(t.Until([&] { return R::IsTerminal(t.Observe(id).Phase); }));
        EXPECT_EQ(t.Observe(id).Phase, committed ? R::StepRunPhase::Committed : R::StepRunPhase::Failed)
            << "the publication decided";
        if (!committed) EXPECT_EQ(t.Observe(id).Diagnostic, "stale: the output changed");
    }
}

TEST(StepRuns, AnOwnerDrivenRunDiscardedWithAPageHeldRetiresOnlyOnceItIsConsumed)
{
    GpuFixture t;
    const auto id = t.Start();
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Run));
    ASSERT_TRUE(t.Until([&] { return t.Observe(id).Steps >= 2u; }));
    t.Gpu.Hold = true;
    ASSERT_TRUE(t.Until([&] { return t.Gpu.InFlight; }));
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Discard));
    EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Discarded) << "terminal at once";
    ASSERT_TRUE(t.Until([&] { return t.Gpu.Discards == 1; })) << "what it shows goes with the next pump";
    t.Frames(5);
    EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Discarded);
    EXPECT_EQ(t.Gpu.Discards, 1) << "exactly once";
    EXPECT_FALSE(t.Gpu.Alive.expired()) << "its callables wait for the held page";
    t.Gpu.Hold = false;
    ASSERT_TRUE(t.Until([&] { return t.Gpu.Alive.expired(); }));
    EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Discarded);
    EXPECT_EQ(t.Gpu.Discards, 1);
}

TEST(StepRuns, AnOwnersPreviewsFollowTheRunsTimeLimit)
{
    auto now = std::make_shared<std::atomic<std::int64_t>>(0);
    GpuFixture t;
    const auto id = t.Start({.Clock = [now] { return std::chrono::steady_clock::time_point{std::chrono::milliseconds{now->load()}}; }});
    EXPECT_TRUE(t.Gpu.Driver.MayPreview());
    EXPECT_TRUE(t.Gpu.Driver.MayPreview()) << "asking counts nothing and does not start the time limit";
    EXPECT_EQ(t.Observe(id).PreviewCopies, 0u);
    t.Gpu.Driver.Previewed();
    EXPECT_FALSE(t.Gpu.Driver.MayPreview()) << "within 33 ms of the submitted copy";
    now->fetch_add(40);
    EXPECT_TRUE(t.Gpu.Driver.MayPreview());
    t.Gpu.Driver.Previewed();
    EXPECT_EQ(t.Observe(id).PreviewCopies, 2u);
}

TEST(StepRuns, ACommitAnswerOutsideAnAcceptIsIgnored)
{
    GpuFixture t;
    t.Gpu.Answer = R::StepRunCommit::Pending;
    const auto id = t.Start();
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Step, 2u));
    ASSERT_TRUE(t.Until([&] { return t.Observe(id).Phase == R::StepRunPhase::Paused; }));
    t.Gpu.Driver.CommitFinished(true); // not accepting: no Commit started
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Accept));
    ASSERT_TRUE(t.Until([&] { return t.Gpu.Commits == 1; }));
    t.Frames(5);
    EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Accepting) << "the stray answer does not decide the pending Commit";
    t.Gpu.Driver.CommitFinished(true);
    ASSERT_TRUE(t.Until([&] { return R::IsTerminal(t.Observe(id).Phase); }));
    EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Committed);
}

TEST(StepRuns, AnOwnerWaitsForARestartAndARetiredRunIgnoresItsHandle)
{
    for (const bool pauseRestart : {false, true})
    {
        SCOPED_TRACE(pauseRestart);
        GpuFixture t;
        const auto id = t.Start();
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Run));
        ASSERT_TRUE(t.Until(
            [&]
            {
                return t.Observe(id).Steps >= 2u;
            }));
        t.Gpu.Hold = true;
        ASSERT_TRUE(t.Until(
            [&]
            {
                return t.Gpu.InFlight;
            }));
        int inits = 0;
        std::string refusal;
        t.Gpu.Force = R::StepOutcome::Converged;
        ASSERT_TRUE(t.F.Runs.Restart(
            id,
            [&](std::string&)
            {
                ++inits;
                t.Gpu.Solve->X = 0.0;
                t.Gpu.Force.reset();
                return true;
            },
            refusal));
        EXPECT_FALSE(t.F.Runs.Control(id, R::StepRunAction::Run, 1u, refusal));
        EXPECT_FALSE(t.F.Runs.Control(id, R::StepRunAction::Step, 1u, refusal));
        EXPECT_FALSE(t.Gpu.Driver.MayStart()) << "a restart waits";
        EXPECT_FALSE(t.Gpu.Driver.MayPreview()) << "nothing of the older computation shows";
        t.Frames(5);
        EXPECT_EQ(inits, 0) << "not while a page is held";
        t.Gpu.Hold = false;
        if (pauseRestart)
        {
            t.Gpu.Frame(); // old convergence arrived; its restart has not been pumped yet
            ASSERT_TRUE(t.Control(id, R::StepRunAction::Pause));
        }
        ASSERT_TRUE(t.Until(
            [&]
            {
                return inits == 1;
            }))
            << "the held page was the older computation's last";
        t.Frames(3);
        EXPECT_EQ(inits, 1) << "restarted once";
        if (pauseRestart)
        {
            EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Ready);
            EXPECT_EQ(t.Observe(id).Steps, 0u);
            ASSERT_TRUE(t.Control(id, R::StepRunAction::Run));
        }
        ASSERT_TRUE(t.Until(
            [&]
            {
                return inits == 1 && t.Observe(id).Steps >= 1u;
            }));
        EXPECT_EQ(t.Observe(id).Generation, 1u);
        EXPECT_EQ(t.Observe(id).Metrics->Records().front().Values[0], 1.0) << "the new generation starts from x = 0";
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Discard));
        ASSERT_TRUE(t.Until(
            [&]
            {
                return t.Gpu.Alive.expired();
            }))
            << "retired";
        const auto retired = t.Observe(id);
        t.Gpu.Driver.Recording(true);
        EXPECT_FALSE(t.Gpu.Driver.Stepped({.Values = {9.0, 0.0, 0.0, 0.0}}, R::StepOutcome::Continue));
        EXPECT_FALSE(t.Gpu.Driver.MayPreview());
        t.Gpu.Driver.Publishing();
        t.Gpu.Driver.CommitFinished(true);
        const auto after = t.Observe(id);
        EXPECT_EQ(after.Phase, R::StepRunPhase::Discarded);
        EXPECT_EQ(after.Steps, retired.Steps);
        EXPECT_FALSE(after.SegmentInFlight);
        EXPECT_EQ(after.PreviewCopies, retired.PreviewCopies);
    }
}

// ARCH-023: the shared live preview cadence. While computing an owner may preview only every PreviewEvery iterations;
// at a rest it may preview its boundary; with 0 never, and a worker run then makes no copy at all while its result
// stays the same.
TEST(StepRuns, PreviewEveryLimitsLivePreviewsAndZeroTurnsThemOffWithoutChangingTheRun)
{
    for (const std::uint32_t every : {3u, 0u})
    {
        GpuFixture t;
        t.Gpu.Solve->Tolerance = 0.0; // never converges
        t.Gpu.AskPreview = true;
        const auto id = t.Start({.Execution = {.IterationCap = 10u, .PreviewEvery = every, .PreviewIntervalMs = 0u}});
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Step, 8u));
        ASSERT_TRUE(t.Until(
            [&]
            {
                return t.Observe(id).Phase == R::StepRunPhase::Paused;
            }));
        ASSERT_EQ(t.Observe(id).Steps, 8u);
        const std::vector<std::uint32_t> expected =
            every ? std::vector<std::uint32_t>{3u, 6u} : std::vector<std::uint32_t>{};
        EXPECT_EQ(t.Gpu.Previewed, expected) << "every " << every;
        const bool rest = t.Gpu.Driver.MayPreview();
        EXPECT_EQ(rest, every != 0u) << "a rest shows its boundary unless live preview is off";
        if (rest)
            t.Gpu.Driver.Previewed();
        EXPECT_EQ(t.Observe(id).PreviewCopies, every ? 3u : 0u);
    }
    const auto reference = Uninterrupted();
    for (const std::uint32_t every : {0u, 4u})
    {
        auto problem = std::make_shared<Problem>();
        Probe probe;
        Fixture f;
        const auto id = f.Start(problem, probe, {.Execution = {.PreviewEvery = every, .PreviewIntervalMs = 0u}});
        // Off: stepped, so every step also rests (a rest copies unless off). On: run, so copies follow the cadence.
        while (every == 0u && f.Observe(id).End == R::StepRunEnd::None)
        {
            ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, 1u));
            ASSERT_TRUE(f.Settle(id));
        }
        if (every != 0u)
        {
            ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
            ASSERT_TRUE(f.Settle(id));
        }
        for (int i = 0; i < 5; ++i)
            f.Jobs.Frame();
        EXPECT_EQ(f.Trace(id), reference) << "the result does not depend on live preview";
        if (every == 0u)
            EXPECT_EQ(f.Observe(id).PreviewCopies, 0u) << "off: no copy, not even at a rest";
        else
        {
            EXPECT_GT(f.Observe(id).PreviewCopies, 0u);
            for (const auto step : probe.ShownSteps)
                EXPECT_TRUE(step % every == 0u || step == reference.size()) << step;
        }
    }
}

// An owner that starts its Commit itself (a typed or automatic Accept) makes it the run's one Commit: the run is
// Accepting, a second Accept and further steps are refused, the run's own Commit is never called and the owner's
// answer decides. Not while the run computes.
TEST(StepRuns, AnOwnerStartedCommitIsTheRunsOneAccept)
{
    GpuFixture t;
    const auto id = t.Start();
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Run));
    ASSERT_TRUE(t.Until(
        [&]
        {
            return t.Gpu.InFlight;
        }));
    EXPECT_FALSE(t.Gpu.Driver.Committing()) << "not while computing";
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Pause));
    ASSERT_TRUE(t.Until(
        [&]
        {
            return t.Observe(id).Phase == R::StepRunPhase::Paused;
        }));
    ASSERT_TRUE(t.Gpu.Driver.Committing());
    EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Accepting);
    EXPECT_TRUE(t.Gpu.Driver.Committing()) << "already its Commit";
    std::string refusal;
    EXPECT_FALSE(t.F.Runs.Control(id, R::StepRunAction::Accept, 1u, refusal)) << "one Accept";
    EXPECT_FALSE(t.F.Runs.Control(id, R::StepRunAction::Step, 1u, refusal));
    t.Frames(5);
    EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Accepting);
    EXPECT_EQ(t.Gpu.Commits, 0) << "the owner's Commit is under way; the run's own is not called";
    t.Gpu.Driver.CommitFinished(true);
    ASSERT_TRUE(t.Until(
        [&]
        {
            return R::IsTerminal(t.Observe(id).Phase);
        }));
    EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Committed);
    EXPECT_FALSE(t.Gpu.Driver.Committing()) << "a terminal run starts nothing";
}

// ARCH-023 B9: the shared execution options. A ready run (the default start) computes nothing until asked: one Step is
// exactly one logical step with no earlier automatic iteration, for a worker and an owner-driven run alike; a run that
// starts running computes its whole sequence without a control. Predictions: ready -> 0 steps after 10 frames, Step(1)
// -> exactly step 1 (x = 1); run -> the uninterrupted trace. Mutation: making Start ignore Execution.Start (always
// running, or never) fails the first or the second half.
TEST(StepRuns, AReadyRunComputesNothingUntilOneStepAndARunStartComputesAtOnce)
{
    const auto reference = Uninterrupted();
    {
        auto problem = std::make_shared<Problem>();
        Probe probe;
        Fixture f;
        const auto id = f.Start(problem, probe);
        for (int i = 0; i < 10; ++i)
            f.Jobs.Frame();
        EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Ready);
        EXPECT_EQ(f.Observe(id).Steps, 0u) << "initialized only";
        EXPECT_EQ(f.Jobs.Submitted, 1) << "the holder alone: no segment was submitted";
        ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, 1u));
        ASSERT_TRUE(f.Settle(id));
        for (int i = 0; i < 5; ++i)
            f.Jobs.Frame();
        ASSERT_EQ(f.Trace(id).size(), 1u) << "exactly one logical step";
        EXPECT_EQ(f.Trace(id).front(), reference.front());
        EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Paused);
    }
    {
        auto problem = std::make_shared<Problem>();
        Probe probe;
        Fixture f;
        const auto id = f.Start(problem, probe, {.Execution = {.Start = R::StepRunStart::Run}});
        EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Running) << "running from its start";
        ASSERT_TRUE(f.Jobs.FramesUntil(
            [&]
            {
                return f.Observe(id).End != R::StepRunEnd::None && !f.Observe(id).SegmentInFlight;
            }));
        EXPECT_EQ(f.Trace(id), reference);
    }
    {
        GpuFixture t;
        const auto id = t.Start();
        t.Frames(10);
        EXPECT_EQ(t.Observe(id).Steps, 0u);
        EXPECT_EQ(t.Gpu.Iterations, 0u) << "the owner recorded nothing";
        EXPECT_FALSE(t.Gpu.Driver.MayStart());
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Step, 1u));
        ASSERT_TRUE(t.Until(
            [&]
            {
                return t.Observe(id).Phase == R::StepRunPhase::Paused;
            }));
        t.Frames(5);
        EXPECT_EQ(t.Observe(id).Steps, 1u);
        EXPECT_EQ(t.Gpu.Iterations, 1u);
        EXPECT_EQ(t.Observe(id).Metrics->Records().front().Values[0], 1.0) << "the first iteration from x = 0";
    }
}

// Restart through run_control while a step is in flight: the owner's initializer runs only once that step ended, the
// run is a new generation from step 0 (it was running, so it runs on), the older step counts nowhere and none of its
// copies shows. Without an initializer restart is refused and not offered. Predictions: inits 0 while held, then 1;
// generation 1; the new generation's trace equals the uninterrupted one (x reset to 0); every copy shown after the
// request is generation 1. Mutations: dropping the in-flight wait (init while held) or not resetting the log fails.
TEST(StepRuns, ARestartIsANewGenerationThatIgnoresTheOlderStepInFlight)
{
    const auto reference = Uninterrupted({.Execution = {.PreviewIntervalMs = 0u}});
    std::mutex mutex;
    std::condition_variable wake;
    bool inside = false, release = false;
    int inits = 0;
    auto problem = std::make_shared<Problem>();
    Probe probe;
    Fixture f;
    R::AgentOperationRegistry registry;
    f.Runs.RegisterOperations(registry);
    problem->InsideStep = [&]
    {
        std::unique_lock lock(mutex);
        if (release)
            return;
        inside = true;
        wake.notify_all();
        wake.wait_for(lock,
            10s,
            [&]
            {
                return release;
            });
    };
    const auto id = f.Start(problem,
        probe,
        {.Execution = {.PreviewIntervalMs = 0u}},
        true,
        [&inits, problem](std::string&)
        {
            ++inits;
            problem->X = 0.0;
            return true;
        });
    EXPECT_TRUE(f.Observe(id).CanRestart);
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
    ASSERT_TRUE(f.Jobs.FramesUntil(
        [&]
        {
            std::lock_guard lock(mutex);
            return inside;
        }));
    const auto out =
        R::InvokeAgentOperation(registry, "run_control", {}, Json{{"run", id}, {"action", "restart"}}.dump(), false);
    ASSERT_FALSE(out.IsError) << out.Text;
    const std::size_t shownBefore = probe.ShownGenerations.size();
    for (int i = 0; i < 5; ++i)
        f.Jobs.Frame();
    EXPECT_EQ(inits, 0) << "not while the older step is in flight";
    EXPECT_EQ(f.Observe(id).Generation, 0u);
    {
        std::lock_guard lock(mutex);
        release = true;
    }
    wake.notify_all();
    ASSERT_TRUE(f.Jobs.FramesUntil(
        [&]
        {
            return f.Observe(id).Generation == 1u;
        }));
    ASSERT_TRUE(f.Jobs.FramesUntil(
        [&]
        {
            return f.Observe(id).End != R::StepRunEnd::None && !f.Observe(id).SegmentInFlight;
        }));
    for (int i = 0; i < 5; ++i)
        f.Jobs.Frame();
    EXPECT_EQ(inits, 1);
    EXPECT_EQ(f.Trace(id), reference) << "the new generation from step 1 only";
    for (std::size_t i = shownBefore; i < probe.ShownGenerations.size(); ++i)
        EXPECT_EQ(probe.ShownGenerations[i], 1u) << "no copy of the older computation shows";

    auto other = std::make_shared<Problem>();
    Probe otherProbe;
    const auto plain = f.Start(other, otherProbe);
    EXPECT_FALSE(f.Observe(plain).CanRestart);
    std::string refusal;
    EXPECT_FALSE(f.Runs.Control(plain, R::StepRunAction::Restart, 1u, refusal));
    EXPECT_NE(refusal.find("cannot restart"), std::string::npos) << refusal;
}

// The host's control row and MCP run_control issue the same StepRunsService::Control calls: two identical runs, one
// driven by the row's buttons (ImGui), one by run_control, end every action in the same observation (phase, end, step
// count, generation, metric trace), refusals included. Mutation: a row that issues another action or count (say Step
// with 1 instead of its count) fails the comparison.
TEST(StepRuns, TheControlRowAndRunControlIssueIdenticalActions)
{
    Fixture f;
    R::AgentOperationRegistry registry;
    f.Runs.RegisterOperations(registry);
    auto uiProblem = std::make_shared<Problem>(), mcpProblem = std::make_shared<Problem>();
    Probe uiProbe, mcpProbe;
    const auto restartOf = [](const std::shared_ptr<Problem>& problem)
    {
        return [problem](std::string&)
        {
            problem->X = 0.0;
            return true;
        };
    };
    const auto ui = f.Start(uiProblem, uiProbe, {}, false, restartOf(uiProblem));
    const auto mcp = f.Start(mcpProblem, mcpProbe, {}, false, restartOf(mcpProblem));
    TestSupport::ImGuiFrameScope gui;
    R::StepRunControlsState row;
    const auto press = [&](const char* button, const std::uint32_t count)
    {
        row.StepCount = count;
        for (int frame = 0; frame < 3; ++frame)
        {
            if (frame > 0 || ImGui::GetFrameCount() > 1)
                gui.NextFrame();
            if (auto* window = ImGui::FindWindowByName("Row"); window && frame == 1)
                ImGui::ActivateItemByID(window->GetID(button));
            ImGui::Begin("Row");
            (void)R::DrawStepRunControls(f.Runs, ui, {}, row);
            ImGui::End();
        }
    };
    const auto call = [&](const char* action, const std::uint32_t count)
    {
        return R::InvokeAgentOperation(
            registry, "run_control", {}, Json{{"run", mcp}, {"action", action}, {"count", count}}.dump(), false);
    };
    const auto same = [&](const char* what)
    {
        SCOPED_TRACE(what);
        ASSERT_TRUE(f.Settle(ui));
        ASSERT_TRUE(f.Settle(mcp));
        // Both settle in the same drains; an Accept's commit lands with its holder's completion.
        ASSERT_TRUE(f.Jobs.FramesUntil(
            [&]
            {
                return f.Observe(ui).Phase == f.Observe(mcp).Phase;
            }));
        for (int i = 0; i < 5; ++i)
            f.Jobs.Frame();
        const auto a = f.Observe(ui), b = f.Observe(mcp);
        EXPECT_EQ(a.Phase, b.Phase);
        EXPECT_EQ(a.End, b.End);
        EXPECT_EQ(a.Steps, b.Steps);
        EXPECT_EQ(a.Generation, b.Generation);
        EXPECT_EQ(f.Trace(ui), f.Trace(mcp));
    };
    const struct
    {
        const char* Button;
        const char* Action;
        std::uint32_t Count;
        bool Accepted;
    } kSequence[]{{"Step", "step", 3u, true},
        {"Stop", "stop", 1u, true},
        {"Run", "run", 1u, false},
        {"Restart", "restart", 1u, true},
        {"Step", "step", 2u, true},
        {"Accept", "accept", 1u, true}};
    for (const auto& action : kSequence)
    {
        row.Message.clear();
        press(action.Button, action.Count);
        const auto answer = call(action.Action, action.Count);
        EXPECT_EQ(answer.IsError, !action.Accepted) << action.Action << ": " << answer.Text;
        EXPECT_EQ(row.Message.empty(), action.Accepted) << action.Button << ": " << row.Message;
        if (!action.Accepted)
            EXPECT_EQ(row.Message, answer.Text) << "the same refusal";
        same(action.Action);
    }
    ASSERT_TRUE(f.Jobs.FramesUntil(
        [&]
        {
            return R::IsTerminal(f.Observe(ui).Phase) && R::IsTerminal(f.Observe(mcp).Phase);
        }));
    EXPECT_EQ(f.Observe(ui).Phase, R::StepRunPhase::Committed);
    EXPECT_EQ(f.Observe(mcp).Phase, R::StepRunPhase::Committed);
    EXPECT_EQ(f.Observe(ui).Generation, 1u);
    EXPECT_EQ(f.Observe(ui).Steps, 2u) << "the restarted generation's two steps";
    EXPECT_EQ(uiProbe.Commits, 1);
    EXPECT_EQ(mcpProbe.Commits, 1);
}

// The publication boundary (H3): an owner's publication asks its run first. A discard that won before it leaves
// nothing to publish (false); once the publication started a discard is refused. An unbound handle (no run controls
// the owner) always publishes. Mutation: Publishing returning true for an ended run fails the first block.
TEST(StepRuns, APublicationStartsOnlyWhileItsRunHasNotEnded)
{
    EXPECT_TRUE(R::StepRunDriver{}.Publishing()) << "no run controls this owner";
    for (const bool discardFirst : {true, false})
    {
        SCOPED_TRACE(discardFirst);
        GpuFixture t;
        t.Gpu.Answer = R::StepRunCommit::Pending;
        const auto id = t.Start();
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Step, 2u));
        ASSERT_TRUE(t.Until(
            [&]
            {
                return t.Observe(id).Phase == R::StepRunPhase::Paused;
            }));
        ASSERT_TRUE(t.Control(id, R::StepRunAction::Accept));
        ASSERT_TRUE(t.Until(
            [&]
            {
                return t.Observe(id).Phase == R::StepRunPhase::Accepting;
            }));
        std::string refusal;
        if (discardFirst)
        {
            ASSERT_TRUE(t.Control(id, R::StepRunAction::Discard));
            EXPECT_FALSE(t.Gpu.Driver.Publishing()) << "the discard won: nothing may be published";
            t.Gpu.Driver.CommitFinished(true); // a late answer counts for nothing
            ASSERT_TRUE(t.Until(
                [&]
                {
                    return t.Gpu.Discards == 1;
                }));
            EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Discarded);
        }
        else
        {
            EXPECT_TRUE(t.Gpu.Driver.Publishing());
            EXPECT_FALSE(t.F.Runs.Control(id, R::StepRunAction::Discard, 1u, refusal)) << "the publication decides";
            t.Gpu.Driver.CommitFinished(true);
            ASSERT_TRUE(t.Until(
                [&]
                {
                    return R::IsTerminal(t.Observe(id).Phase);
                }));
            EXPECT_EQ(t.Observe(id).Phase, R::StepRunPhase::Committed);
            EXPECT_EQ(t.Gpu.Discards, 0);
        }
    }
}

// An owner that goes away (its module shuts down with a page still in flight) abandons its run: discarded at once, its
// callables dropped at once, nothing later counts. Mutation: Abandon without dropping the callables (Settle) leaves
// the owner's token alive.
TEST(StepRuns, AnAbandonedRunDropsItsOwnersCallablesAtOnceWhateverIsInFlight)
{
    GpuFixture t;
    const auto id = t.Start();
    ASSERT_TRUE(t.Control(id, R::StepRunAction::Run));
    ASSERT_TRUE(t.Until(
        [&]
        {
            return t.Observe(id).Steps >= 2u;
        }));
    t.Gpu.Hold = true;
    ASSERT_TRUE(t.Until(
        [&]
        {
            return t.Gpu.InFlight;
        }));
    ASSERT_FALSE(t.Gpu.Alive.expired());
    t.Gpu.Driver.Abandon();
    const auto abandoned = t.Observe(id);
    EXPECT_EQ(abandoned.Phase, R::StepRunPhase::Discarded);
    EXPECT_FALSE(abandoned.SegmentInFlight) << "what the owner had in flight counts as consumed";
    EXPECT_FALSE(abandoned.CanRestart);
    EXPECT_EQ(t.Gpu.Discards, 1);
    EXPECT_TRUE(t.Gpu.Alive.expired()) << "the owner's callables are gone at once";
    t.Gpu.Hold = false;
    t.Frames(5);
    EXPECT_EQ(t.Observe(id).Steps, abandoned.Steps) << "a late page counts nowhere";
    EXPECT_EQ(t.Gpu.Discards, 1);
}

// MCP arguments, config rows and the run admission share one table: the reader accepts exactly the rows' ranges and
// tokens, keeps an absent value and changes nothing on a refusal; the schema carries the same bounds; a start with
// options outside them is refused. Mutation: a reader that clamps instead of refusing fails the second block.
TEST(StepRuns, ExecutionArgumentsAndAdmissionShareTheFieldRows)
{
    R::StepRunExecution options{};
    std::string refusal;
    ASSERT_TRUE(R::ReadStepRunExecutionArguments(
        R"({"max_iterations":7,"preview_every":0,"preview_interval_ms":16,"auto_accept":"end","start":"run"})",
        100u,
        options,
        refusal))
        << refusal;
    EXPECT_EQ(options,
        (R::StepRunExecution{.IterationCap = 7u,
            .PreviewEvery = 0u,
            .PreviewIntervalMs = 16u,
            .AutoAccept = R::StepRunAutoAccept::End,
            .Start = R::StepRunStart::Run}));
    const auto kept = options;
    for (const char* bad : {R"({"max_iterations":101})",
             R"({"max_iterations":0})",
             R"({"preview_every":-1})",
             R"({"preview_interval_ms":60001})",
             R"({"auto_accept":"sometimes"})",
             R"({"start":"later"})",
             R"({"start":1})"})
    {
        EXPECT_FALSE(R::ReadStepRunExecutionArguments(bad, 100u, options, refusal)) << bad;
        EXPECT_EQ(options, kept) << bad;
    }
    ASSERT_TRUE(R::ReadStepRunExecutionArguments("{}", 100u, options, refusal));
    EXPECT_EQ(options, kept) << "absent arguments keep their values";

    const auto schema = Json::parse("{" + R::StepRunExecutionSchemaProperties(100u) + "}");
    EXPECT_EQ(schema["max_iterations"]["minimum"], 1);
    EXPECT_EQ(schema["max_iterations"]["maximum"], 100);
    EXPECT_EQ(schema["preview_every"]["maximum"], R::kMaxStepRunPreviewEvery);
    EXPECT_EQ(schema["preview_interval_ms"]["maximum"], R::kMaxStepRunPreviewIntervalMs);
    EXPECT_EQ(schema["auto_accept"]["enum"], Json::array({"never", "converged", "end"}));
    EXPECT_EQ(schema["start"]["enum"], Json::array({"ready", "run"}));

    EXPECT_TRUE(R::StepRunExecutionRefusal({}, 100u).empty());
    EXPECT_FALSE(R::StepRunExecutionRefusal({.IterationCap = 0u}, 100u).empty());
    Fixture f;
    auto problem = std::make_shared<Problem>();
    Probe probe;
    std::string startRefusal;
    // Admission enforces the method range; the executor also supports an initialized identity result.
    EXPECT_NE(
        f.Runs.Start({.Execution = {.IterationCap = 0u}}, Callbacks(problem, probe), f.Jobs.Bind(), startRefusal), 0u);
    EXPECT_TRUE(startRefusal.empty());
}

// ARCH-023: a GPU run's preview copy needs a device slot its owner offered. Without one the worker makes no copy:
// nothing is published or counted, and the run computes to its end with no frame at all. Once a slot is offered the
// copy carries its fence to Show. Mutation: counting (or publishing) a copy that was not made gives copies > 0.
TEST(StepRuns, ACopyWithoutAFreeSlotIsNeitherMadeNorCountedAndTheRunGoesOn)
{
    const auto reference = Uninterrupted();
    auto problem = std::make_shared<Problem>();
    Probe probe;
    Fixture f;
    std::atomic<bool> offered{false};
    std::atomic<int> refused{0};
    std::vector<std::uint64_t> shownFences;
    auto callbacks = Callbacks(problem, probe);
    callbacks.Preview = [&](R::StepPreview& preview)
    {
        if (!offered.load())
        {
            ++refused;
            return false;
        }
        preview.Values.assign(1, static_cast<float>(problem->X));
        preview.Fence = 42u; // a device copy's completion
        return true;
    };
    callbacks.Show = [&](const R::StepPreview& preview)
    {
        shownFences.push_back(preview.Fence);
        return R::StepRunShow::Shown;
    };
    std::string refusal;
    const auto id = f.Runs.Start({.Label = "gpu preview",
                                     .Owner = "test",
                                     .Execution = {.PreviewEvery = 1u, .PreviewIntervalMs = 0u},
                                     .MetricNames = {"x", "change", "", ""}},
        std::move(callbacks),
        f.Jobs.Bind(),
        refusal);
    ASSERT_NE(id, 0u) << refusal;
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
    ASSERT_TRUE(f.Jobs.FramesUntil(
        [&]
        {
            return f.Observe(id).WorkerInFlight;
        }));
    // The holder's asynchronous work must have completed before its pump can submit the worker; no frames now.
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (f.Observe(id).Steps < reference.size() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    EXPECT_EQ(f.Observe(id).Steps, reference.size()) << "no slot never stalls the computation";
    EXPECT_EQ(f.Observe(id).PreviewCopies, 0u) << "no copy made, none counted";
    EXPECT_GE(refused.load(), 3);
    ASSERT_TRUE(f.Settle(id));
    EXPECT_TRUE(shownFences.empty()) << "nothing was published to show";
    offered = true; // the owner offers a slot: the ended run's state shows at a rest, with its fence
    ASSERT_TRUE(f.Jobs.FramesUntil(
        [&]
        {
            return !shownFences.empty();
        }));
    EXPECT_EQ(shownFences.front(), 42u);
    EXPECT_EQ(f.Observe(id).PreviewCopies, 1u);
    EXPECT_EQ(f.Trace(id), reference);
}

namespace
{
    // A worker run whose Commit starts a publication (a GPU readback) and answers Pending.
    struct PendingWorkerRun
    {
        std::shared_ptr<Problem> Work{std::make_shared<Problem>()};
        Probe Seen{};
        Fixture F{};
        std::uint64_t Id{};
        R::StepRunDriver Driver{};

        PendingWorkerRun()
        {
            auto callbacks = Callbacks(Work, Seen);
            callbacks.Commit = [this](std::string&)
            {
                ++Seen.Commits;
                return R::StepRunCommit::Pending;
            };
            std::string refusal;
            Id = F.Runs.Start({.Label = "pending", .Owner = "test", .MetricNames = {"x", "change", "", ""}},
                std::move(callbacks),
                F.Jobs.Bind(),
                refusal);
            EXPECT_NE(Id, 0u) << refusal;
            Driver = F.Runs.Driver(Id);
            Work.reset(); // only the run's Step keeps the owner's problem (Seen.Alive)
        }

        // Two steps, then Accept: the holder ends with the Commit pending.
        void Accept()
        {
            ASSERT_TRUE(F.Control(Id, R::StepRunAction::Step, 2u));
            ASSERT_TRUE(F.Settle(Id));
            ASSERT_TRUE(F.Control(Id, R::StepRunAction::Accept));
            ASSERT_TRUE(F.Jobs.FramesUntil(
                [&]
                {
                    return Seen.Commits == 1;
                }));
            for (int i = 0; i < 5; ++i)
                F.Jobs.Frame();
        }
    };
}

// ARCH-023: a worker run's Commit may answer Pending (K-Means reads its labels back): the run stays Accepting after
// its holder ended, and the owner's answer decides it once. Mutation: the former holder completion took a Pending
// answer as refused and ended the run Discarded ("cancelled") before the publication.
TEST(StepRuns, AWorkerRunsPendingCommitIsDecidedByItsOwnersAnswer)
{
    for (const bool committed : {true, false})
    {
        PendingWorkerRun t;
        t.Accept();
        EXPECT_EQ(t.F.Observe(t.Id).Phase, R::StepRunPhase::Accepting) << "pending after the holder ended";
        EXPECT_EQ(t.Seen.Commits, 1);
        std::string refusal;
        EXPECT_FALSE(t.F.Runs.Control(t.Id, R::StepRunAction::Accept, 1u, refusal)) << "one Accept";
        EXPECT_TRUE(t.Driver.Publishing());
        EXPECT_FALSE(t.F.Runs.Control(t.Id, R::StepRunAction::Discard, 1u, refusal)) << "the publication decides";
        t.Driver.CommitFinished(committed, committed ? "" : "stale: the output changed");
        EXPECT_EQ(t.F.Observe(t.Id).Phase, committed ? R::StepRunPhase::Committed : R::StepRunPhase::Failed);
        EXPECT_EQ(t.Seen.Discards, committed ? 0 : 1);
        EXPECT_TRUE(t.Seen.Alive.expired()) << "the owner's callables are dropped once it is answered";
    }
}

// A discard before the pending publication started wins; with no pump left, the owner's answer (its abandoned
// readback) settles the run in its context: Discard runs once, the callables go. Mutation: without the settle in
// CommitFinished the owner's Discard never runs and its callables stay.
TEST(StepRuns, ADiscardBeforeAPendingWorkerPublicationWinsAndTheOwnersAnswerSettlesIt)
{
    PendingWorkerRun t;
    t.Accept();
    ASSERT_TRUE(t.F.Control(t.Id, R::StepRunAction::Discard));
    EXPECT_EQ(t.F.Observe(t.Id).Phase, R::StepRunPhase::Discarded);
    EXPECT_FALSE(t.Driver.Publishing()) << "nothing may be published any more";
    t.Driver.CommitFinished(true); // the owner's late answer counts for nothing but settles the run
    EXPECT_EQ(t.F.Observe(t.Id).Phase, R::StepRunPhase::Discarded);
    EXPECT_EQ(t.Seen.Discards, 1);
    EXPECT_TRUE(t.Seen.Alive.expired());
    t.Driver.CommitFinished(false);
    EXPECT_EQ(t.Seen.Discards, 1) << "once";
}

// A zero cap preserves methods whose old iteration range included the identity operation.
TEST(StepRuns, ZeroCapEndsBeforeAnyWorkerOrOwnerStepAndAcceptsTheInitializedState)
{
    for (const auto action : {R::StepRunAction::Run, R::StepRunAction::Step})
    {
        auto problem = std::make_shared<Problem>();
        Probe probe;
        Fixture f;
        const auto id = f.Start(problem, probe, {.Execution = {.IterationCap = 0u}});
        ASSERT_NE(id, 0u);
        EXPECT_FALSE(f.Control(id, R::StepRunAction::Accept));
        ASSERT_TRUE(f.Control(id, action));
        EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Paused);
        EXPECT_EQ(f.Observe(id).End, R::StepRunEnd::IterationCap);
        for (int i = 0; i != 10; ++i)
            f.Jobs.Frame();
        EXPECT_EQ(f.Jobs.Submitted, 1);
        EXPECT_EQ(problem->X, 0.0);
        EXPECT_EQ(f.Observe(id).Steps, 0u);
        EXPECT_TRUE(f.Trace(id).empty());
        EXPECT_EQ(f.Observe(id).PreviewCopies, 0u);
        ASSERT_TRUE(f.Control(id, R::StepRunAction::Accept));
        ASSERT_TRUE(f.Jobs.FramesUntil(
            [&]
            {
                return f.Observe(id).Phase == R::StepRunPhase::Committed;
            }));
        EXPECT_EQ(probe.Commits, 1);
    }
    GpuFixture g;
    const auto id = g.Start({.Execution = {.IterationCap = 0u}});
    ASSERT_NE(id, 0u);
    ASSERT_TRUE(g.Control(id, R::StepRunAction::Run));
    EXPECT_FALSE(g.Gpu.Driver.MayStart()); // before the owner's first frame
    EXPECT_EQ(g.Gpu.Driver.Allowance(), 0u);
    g.Frames(10);
    EXPECT_EQ(g.Observe(id).Steps, 0u);
    EXPECT_FALSE(g.Gpu.InFlight);
    EXPECT_EQ(g.Observe(id).PreviewCopies, 0u);
    ASSERT_TRUE(g.Control(id, R::StepRunAction::Accept));
    ASSERT_TRUE(g.Until(
        [&]
        {
            return g.Observe(id).Phase == R::StepRunPhase::Committed;
        }));
    EXPECT_EQ(g.Gpu.Commits, 1);
}

TEST(StepRuns, ZeroCapAutoAcceptAndRestartNeverScheduleAStep)
{
    for (const auto accept : {R::StepRunAutoAccept::End, R::StepRunAutoAccept::Never, R::StepRunAutoAccept::Converged})
    {
        auto problem = std::make_shared<Problem>();
        Probe probe;
        Fixture f;
        const auto id = f.Start(problem,
            probe,
            {.Execution = {.IterationCap = 0u, .AutoAccept = accept, .Start = R::StepRunStart::Run}},
            true,
            [problem](std::string&)
            {
                problem->X = 0.0;
                return true;
            });
        ASSERT_NE(id, 0u);
        EXPECT_EQ(f.Observe(id).End, R::StepRunEnd::IterationCap);
        if (accept == R::StepRunAutoAccept::End)
        {
            ASSERT_TRUE(f.Jobs.FramesUntil(
                [&]
                {
                    return f.Observe(id).Phase == R::StepRunPhase::Committed;
                }));
            EXPECT_EQ(probe.Commits, 1);
        }
        else
        {
            ASSERT_TRUE(f.Control(id, R::StepRunAction::Restart));
            ASSERT_TRUE(f.Jobs.FramesUntil(
                [&]
                {
                    return f.Observe(id).Generation == 1u;
                }));
            EXPECT_EQ(f.Observe(id).End, R::StepRunEnd::IterationCap);
            EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Paused);
            ASSERT_TRUE(f.Control(id, R::StepRunAction::Discard));
            ASSERT_TRUE(f.Jobs.FramesUntil(
                [&]
                {
                    return probe.Discards == 1;
                }));
            EXPECT_EQ(probe.Commits, 0);
        }
        EXPECT_EQ(f.Jobs.Submitted, 1);
        EXPECT_EQ(problem->X, 0.0);
        EXPECT_EQ(f.Observe(id).Steps, 0u);
    }
}

TEST(StepRuns, OnlyAnOptedInMethodAllowsAZeroIterationCap)
{
    R::StepRunExecution options{};
    std::string refusal;
    EXPECT_EQ(*R::StepRunExecutionFields(100u)[0].Min, 1.0);
    EXPECT_EQ(*R::StepRunExecutionFields(100u, 0u)[0].Min, 0.0);
    ASSERT_TRUE(R::ReadStepRunExecutionArguments(R"({"max_iterations":0})", 100u, options, refusal, 0u));
    EXPECT_EQ(options.IterationCap, 0u);
    EXPECT_TRUE(R::StepRunExecutionRefusal(options, 100u, 0u).empty());
    EXPECT_FALSE(R::StepRunExecutionRefusal(options, 100u).empty());
    const auto schema = Json::parse("{" + R::StepRunExecutionSchemaProperties(100u, 0u) + "}");
    EXPECT_EQ(schema["max_iterations"]["minimum"], 0);
}

TEST(StepRuns, EndingOwnerRecordingDoesNotPauseAWorkerStillInsideItsStep)
{
    auto problem = std::make_shared<Problem>();
    Probe probe;
    Fixture f;
    std::atomic<bool> entered{false}, release{false};
    problem->InsideStep = [&]
    {
        entered = true;
        while (!release.load())
            std::this_thread::yield();
    };
    const auto id = f.Start(problem, probe);
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
    const bool started = f.Jobs.FramesUntil(
        [&]
        {
            return entered.load();
        });
    if (!started)
        release = true;
    ASSERT_TRUE(started);
    EXPECT_TRUE(f.Control(id, R::StepRunAction::Pause));
    f.Runs.Driver(id).Recording(false);
    EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Running);
    EXPECT_TRUE(f.Observe(id).WorkerInFlight);
    EXPECT_FALSE(f.Control(id, R::StepRunAction::Accept));
    release = true;
    ASSERT_TRUE(f.Settle(id));
    EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Paused);
    EXPECT_EQ(f.Observe(id).Steps, 1u);
}

namespace
{
    // ARCH-023: a worker run whose Init is the owner's (Prepared) and whose step may complete several logical steps in
    // one bounded submission (StepBatch, `Budget` at most per call). Steps and allowances are recorded by the worker.
    struct BatchedOwner
    {
        std::atomic<bool> Prepared{false};
        std::atomic<std::uint32_t> Budget{4u};
        std::mutex Mutex{};
        std::vector<std::uint32_t> Allowances{};
        std::atomic<int> SingleSteps{0};
        // A batch may wait here (while Gate is set) until the test releases it.
        std::atomic<bool> Gate{false}, InBatch{false};

        R::StepRunCallbacks Callbacks(const bool preview)
        {
            R::StepRunCallbacks callbacks{.Step =
                                              [this](R::StepRunMetric&)
                {
                    ++SingleSteps;
                    return R::StepOutcome::Continue;
                },
                .StepBatch =
                    [this](R::StepRunMetric& metric, const std::uint32_t allowance, std::uint32_t& completed)
                {
                    {
                        const std::scoped_lock lock{Mutex};
                        Allowances.push_back(allowance);
                    }
                    InBatch = true;
                    while (Gate.load())
                        std::this_thread::sleep_for(1ms);
                    InBatch = false;
                    completed = std::min(allowance, Budget.load());
                    metric.Values = {double(completed), 0.0, 0.0, 0.0};
                    return R::StepOutcome::Continue;
                },
                .Commit =
                    [](std::string&)
                {
                    return R::StepRunCommit::Committed;
                },
                .Discard = [] {},
                .Restart =
                    [this](std::string&)
                {
                    Prepared = false; // a new generation's Init
                    return true;
                },
                .Prepared =
                    [this]
                {
                    return Prepared.load();
                }};
            if (preview)
            {
                callbacks.Preview = [](R::StepPreview& copy)
                {
                    copy.Values = {float(copy.Step)};
                    return true;
                };
                callbacks.Show = [](const R::StepPreview&)
                {
                    return R::StepRunShow::Shown;
                };
            }
            return callbacks;
        }
        std::vector<std::uint32_t> TakeAllowances()
        {
            const std::scoped_lock lock{Mutex};
            return std::exchange(Allowances, {});
        }
    };

    std::uint64_t StartBatched(Fixture& f, BatchedOwner& owner, const R::StepRunExecution execution, const bool preview)
    {
        std::string refusal;
        const auto id = f.Runs.Start({.Label = "batched", .Owner = "test", .Execution = execution},
            owner.Callbacks(preview),
            f.Jobs.Bind(),
            refusal);
        EXPECT_NE(id, 0u) << refusal;
        return id;
    }

    std::vector<std::uint32_t> LoggedSteps(Fixture& f, const std::uint64_t id)
    {
        std::vector<std::uint32_t> steps;
        for (const auto& metric : f.Observe(id).Metrics->Records())
            steps.push_back(metric.Step);
        return steps;
    }
}

// ARCH-023: a worker run with Prepared starts no segment before its owner's Init answered, whatever its controls
// (running at once here), and a restart's new Init is gated the same way. Predictions: 10 frames with Prepared false
// leave Steps 0, no segment and no Step call; Prepared true runs to the cap (5); after a restart (which clears
// Prepared) the new generation again waits with 0 steps, then reaches 5. Mutation: ignoring Prepared in Pump submits the
// segment in the first frame (Steps 5 before Prepared).
TEST(StepRuns, PreparedHoldsEverySegmentUntilTheOwnersInitAnswered)
{
    Fixture f;
    BatchedOwner owner;
    owner.Budget = 1u;
    const auto id =
        StartBatched(f, owner, {.IterationCap = 5u, .PreviewEvery = 0u, .Start = R::StepRunStart::Run}, false);
    for (int i = 0; i < 10; ++i)
        f.Jobs.Frame();
    EXPECT_EQ(f.Observe(id).Steps, 0u);
    EXPECT_FALSE(f.Observe(id).SegmentInFlight);
    EXPECT_TRUE(owner.TakeAllowances().empty()) << "no step before the Init";
    owner.Prepared = true;
    ASSERT_TRUE(f.Settle(id));
    EXPECT_EQ(f.Observe(id).Steps, 5u);
    EXPECT_EQ(f.Observe(id).End, R::StepRunEnd::IterationCap);
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Restart));
    ASSERT_TRUE(f.Jobs.FramesUntil(
        [&]
        {
            return f.Observe(id).Generation == 1u;
        }));
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Run)); // computing again, its Init not answered yet
    for (int i = 0; i < 10; ++i)
        f.Jobs.Frame();
    EXPECT_EQ(f.Observe(id).Steps, 0u) << "the restart's Init is gated too";
    EXPECT_FALSE(f.Observe(id).SegmentInFlight);
    EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Running);
    owner.Prepared = true;
    ASSERT_TRUE(f.Settle(id));
    EXPECT_EQ(f.Observe(id).Steps, 5u);
    EXPECT_EQ(owner.SingleSteps.load(), 0) << "a batched run steps through StepBatch alone";
}

// ARCH-023: StepBatch completes several logical steps at once, within what is left of a step n and of the cap and cut
// at the live preview cadence. Predictions (cap 10, preview every 3, budget 4 per call): Step(7) asks 3, 3, 1 (logged
// steps 3, 6, 7, exactly 7), then Run asks 2, 1 (9, 10) and ends at the cap; without a Preview callable nothing is
// cut: Run asks 10 and completes it in budget-bounded calls 4, 4, 2. Mutation: an allowance ignoring step n or the
// cadence overshoots step 7 or skips a preview step.
TEST(StepRuns, StepBatchCountsExactlyWithinStepNTheCapAndThePreviewCadence)
{
    {
        Fixture f;
        BatchedOwner owner;
        owner.Prepared = true;
        const auto id =
            StartBatched(f, owner, {.IterationCap = 10u, .PreviewEvery = 3u, .Start = R::StepRunStart::Ready}, true);
        ASSERT_TRUE(f.Control(id, R::StepRunAction::Step, 7u));
        ASSERT_TRUE(f.Settle(id));
        EXPECT_EQ(f.Observe(id).Steps, 7u);
        EXPECT_EQ(owner.TakeAllowances(), (std::vector<std::uint32_t>{3u, 3u, 1u}));
        EXPECT_EQ(LoggedSteps(f, id), (std::vector<std::uint32_t>{3u, 6u, 7u}));
        EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Paused);
        ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
        ASSERT_TRUE(f.Settle(id));
        EXPECT_EQ(owner.TakeAllowances(), (std::vector<std::uint32_t>{2u, 1u}));
        EXPECT_EQ(f.Observe(id).Steps, 10u);
        EXPECT_EQ(f.Observe(id).End, R::StepRunEnd::IterationCap);
        EXPECT_EQ(LoggedSteps(f, id), (std::vector<std::uint32_t>{3u, 6u, 7u, 9u, 10u}));
    }
    {
        Fixture f;
        BatchedOwner owner;
        owner.Prepared = true;
        const auto id =
            StartBatched(f, owner, {.IterationCap = 10u, .PreviewEvery = 3u, .Start = R::StepRunStart::Ready}, false);
        ASSERT_TRUE(f.Control(id, R::StepRunAction::Run));
        ASSERT_TRUE(f.Settle(id));
        EXPECT_EQ(owner.TakeAllowances(), (std::vector<std::uint32_t>{10u, 6u, 2u}));
        EXPECT_EQ(LoggedSteps(f, id), (std::vector<std::uint32_t>{4u, 8u, 10u}));
        EXPECT_EQ(f.Observe(id).Steps, 10u);
    }
}

// ARCH-023: a pause or stop arriving while a batch runs applies after it, and every step of that batch counts.
// Predictions: Run, the batch held mid-call, Pause: once released 4 steps count and the run is Paused (not ended);
// Run again, held, Stop: 8 steps, End Stopped, nothing more recorded. Mutation: dropping a batch's steps on a pause
// leaves 0 or 4.
TEST(StepRuns, APauseOrStopDuringABatchCountsTheWholeBatch)
{
    Fixture f;
    BatchedOwner owner;
    owner.Prepared = true;
    const auto id =
        StartBatched(f, owner, {.IterationCap = 20u, .PreviewEvery = 0u, .Start = R::StepRunStart::Ready}, false);
    const auto heldControl = [&](const R::StepRunAction action)
    {
        owner.Gate = true;
        EXPECT_TRUE(f.Control(id, R::StepRunAction::Run));
        EXPECT_TRUE(f.Jobs.FramesUntil(
            [&]
            {
                return owner.InBatch.load();
            }));
        EXPECT_TRUE(f.Control(id, action));
        owner.Gate = false;
        EXPECT_TRUE(f.Settle(id));
    };
    heldControl(R::StepRunAction::Pause);
    EXPECT_EQ(f.Observe(id).Steps, 4u);
    EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Paused);
    EXPECT_EQ(f.Observe(id).End, R::StepRunEnd::None);
    heldControl(R::StepRunAction::Stop);
    EXPECT_EQ(f.Observe(id).Steps, 8u);
    EXPECT_EQ(f.Observe(id).End, R::StepRunEnd::Stopped);
    for (int i = 0; i < 10; ++i)
        f.Jobs.Frame();
    EXPECT_EQ(f.Observe(id).Steps, 8u) << "a stopped run records nothing more";
}

namespace
{
    // ARCH-023: a one-shot worker run (StartOneShotWorkerRun) whose Compute a test can hold inside the step, counting
    // what its owner sees. `Alive` expires once the run dropped every callable of its owner.
    struct OneShotProbe
    {
        std::mutex Mutex{};
        std::condition_variable Wake{};
        bool Inside{false}, Released{false};
        bool Hold{true}, ComputeResult{true};
        int Publishes{0}, Ends{0};
        std::optional<R::StepRunObservation> Ended{};
        std::function<void(const R::StepRunObservation&)> OnEnded{};
        std::weak_ptr<int> Alive{};

        bool IsInside()
        {
            const std::lock_guard lock(Mutex);
            return Inside;
        }
        void Release()
        {
            {
                const std::lock_guard lock(Mutex);
                Released = true;
            }
            Wake.notify_all();
        }
    };

    std::uint64_t StartOneShot(Fixture& f, const std::shared_ptr<OneShotProbe>& p, std::string& refusal)
    {
        auto alive = std::make_shared<int>(0);
        p->Alive = alive;
        R::OneShotWorkerCallbacks callbacks{.Compute =
                                                [p, alive]
            {
                if (p->Hold)
                {
                    std::unique_lock lock(p->Mutex);
                    p->Inside = true;
                    p->Wake.notify_all();
                    p->Wake.wait_for(lock,
                        10s,
                        [&]
                        {
                            return p->Released;
                        });
                }
                return p->ComputeResult;
            },
            .Publish =
                [p](std::string&)
            {
                ++p->Publishes;
                return true;
            },
            .Ended =
                [p](const R::StepRunObservation& run)
            {
                ++p->Ends;
                p->Ended = run;
                if (p->OnEnded)
                    p->OnEnded(run);
            }};
        return R::StartOneShotWorkerRun(f.Runs,
            {.Label = "one-shot",
                .Owner = "test",
                .Output = R::PluginJobOutput{.StableEntityId = 1u},
                .Execution =
                    R::OneShotExecution({.AutoAccept = R::StepRunAutoAccept::Never, .Start = R::StepRunStart::Run})},
            std::move(callbacks),
            f.Jobs.Bind(),
            refusal);
    }

    bool AllJobsTerminal(const Owner& owner)
    {
        for (const auto& [run, tokens] : owner.RunJobs)
            for (const auto token : tokens)
                if (owner.Active(token))
                    return false;
        return true;
    }
}

// T1: a one-shot's end (Ended) waits for the step it holds and for the holder that holds the output: a discard while
// Compute runs ends nothing at once (the owner's state stays), the end comes once after the step returned, nothing is
// published, and a same-output run started right after is admitted; a failed step ends only after its holder, so a
// new run is admitted from its end. Mutations: Finished without the in-flight condition ends during the hold; without
// the holder condition the failed step's end comes while the holder still owns the output (the restart is busy).
TEST(StepRuns, AOneShotEndsAfterItsStepAndItsHolderSoTheOutputIsFreeAgain)
{
    {
        Fixture f;
        f.Jobs.OneOutput = true;
        auto p = std::make_shared<OneShotProbe>();
        std::string refusal;
        const auto id = StartOneShot(f, p, refusal);
        ASSERT_NE(id, 0u) << refusal;
        ASSERT_TRUE(f.Jobs.FramesUntil(
            [&]
            {
                return p->IsInside();
            }));
        ASSERT_TRUE(f.Control(id, R::StepRunAction::Discard));
        for (int i = 0; i < 10; ++i)
            f.Jobs.Frame();
        EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Discarded);
        EXPECT_TRUE(f.Observe(id).WorkerInFlight) << "the step is still held";
        EXPECT_EQ(p->Ends, 0) << "no end while Compute runs";
        EXPECT_FALSE(p->Alive.expired()) << "the owner's captured state stays while the step runs";
        p->Release();
        ASSERT_TRUE(f.Jobs.FramesUntil(
            [&]
            {
                return p->Ends > 0;
            }));
        for (int i = 0; i < 5; ++i)
            f.Jobs.Frame();
        EXPECT_EQ(p->Ends, 1);
        EXPECT_EQ(p->Publishes, 0);
        ASSERT_TRUE(p->Ended);
        EXPECT_EQ(p->Ended->Phase, R::StepRunPhase::Discarded);
        EXPECT_FALSE(p->Ended->SegmentInFlight);
        EXPECT_TRUE(AllJobsTerminal(f.Jobs));
        auto next = std::make_shared<OneShotProbe>();
        next->Hold = false;
        EXPECT_NE(StartOneShot(f, next, refusal), 0u) << "the output is free once the run ended";
    }
    {
        Fixture f;
        f.Jobs.OneOutput = true;
        auto p = std::make_shared<OneShotProbe>();
        p->Hold = false;
        p->ComputeResult = false;
        std::string refusal;
        const auto id = StartOneShot(f, p, refusal);
        ASSERT_NE(id, 0u) << refusal;
        ASSERT_TRUE(f.Jobs.FramesUntil(
            [&]
            {
                return p->Ends > 0;
            }));
        ASSERT_TRUE(p->Ended);
        EXPECT_EQ(p->Ended->Phase, R::StepRunPhase::Failed);
        EXPECT_TRUE(p->Ended->StepFailed) << "the step itself failed (structured, not a diagnostic's wording)";
        EXPECT_FALSE(f.Jobs.OutputBusy()) << "the holder finished before the end";
        auto next = std::make_shared<OneShotProbe>();
        next->Hold = false;
        EXPECT_NE(StartOneShot(f, next, refusal), 0u) << "a failed result's next run is admitted at once";
    }
}

// T2: an owner whose surface cannot cancel (a detached session) still frees the output: the terminal holder keeps it
// while the step is held, then completes once nothing is in flight, and the end comes once. Mutations: without the
// fallback the holder never finishes (the deadline); answering ready without the in-flight check finishes the holder
// during the hold.
TEST(StepRuns, ARefusedCancelLetsTheHolderFinishOnceNothingIsInFlight)
{
    Fixture f;
    f.Jobs.RefuseCancel = true;
    auto p = std::make_shared<OneShotProbe>();
    std::string refusal;
    const auto id = StartOneShot(f, p, refusal);
    ASSERT_NE(id, 0u) << refusal;
    ASSERT_TRUE(f.Jobs.FramesUntil(
        [&]
        {
            return p->IsInside();
        }));
    ASSERT_TRUE(f.Control(id, R::StepRunAction::Discard));
    for (int i = 0; i < 10; ++i)
        f.Jobs.Frame();
    ASSERT_EQ(f.Jobs.Holders.size(), 1u);
    EXPECT_TRUE(f.Jobs.Active(f.Jobs.Holders.front())) << "the holder keeps the output while the step runs";
    EXPECT_EQ(p->Ends, 0);
    p->Release();
    ASSERT_TRUE(f.Jobs.FramesUntil(
        [&]
        {
            return !f.Jobs.OutputBusy() && p->Ends > 0;
        }))
        << "the holder finished although its cancel was refused";
    for (int i = 0; i < 5; ++i)
        f.Jobs.Frame();
    EXPECT_EQ(p->Ends, 1);
    EXPECT_EQ(p->Publishes, 0);
    EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Discarded);
    EXPECT_TRUE(AllJobsTerminal(f.Jobs));
}

// T3 and T4: a cancel of the run's jobs (the Jobs window, an unload) finalizes the holder before the held step; the end
// comes only after the step returned, once, and then the owner's state is gone. Inside the end every control of the
// run is refused and changes nothing, and a same-output run started there (the cancel path) is admitted. Mutation:
// Finished from the holder's completion fires during the hold.
TEST(StepRuns, ACancelledOneShotEndsAfterItsStepAndItsEndMayStartTheNextRun)
{
    Fixture f;
    f.Jobs.OneOutput = true;
    auto p = std::make_shared<OneShotProbe>();
    std::string refusal;
    const auto id = StartOneShot(f, p, refusal);
    ASSERT_NE(id, 0u) << refusal;
    auto next = std::make_shared<OneShotProbe>();
    next->Hold = false;
    std::uint64_t restarted = 0u;
    bool controlled = false;
    p->OnEnded = [&](const R::StepRunObservation& run)
    {
        std::string why;
        controlled = f.Runs.Control(id, R::StepRunAction::Restart, 1u, why) ||
            f.Runs.Control(id, R::StepRunAction::Discard, 1u, why) ||
            f.Runs.Control(id, R::StepRunAction::Accept, 1u, why);
        EXPECT_EQ(f.Observe(id).Phase, run.Phase) << "a refused control changes nothing";
        restarted = StartOneShot(f, next, why);
        EXPECT_NE(restarted, 0u) << why;
    };
    ASSERT_TRUE(f.Jobs.FramesUntil(
        [&]
        {
            return p->IsInside();
        }));
    ASSERT_TRUE(f.Jobs.Cancel({f.Jobs.Holders.front()}));
    ASSERT_TRUE(f.Jobs.FramesUntil(
        [&]
        {
            return !f.Jobs.Active(f.Jobs.Holders.front());
        }));
    for (int i = 0; i < 5; ++i)
        f.Jobs.Frame();
    EXPECT_EQ(p->Ends, 0) << "the holder finished first; the step still runs";
    EXPECT_FALSE(p->Alive.expired());
    p->Release();
    ASSERT_TRUE(f.Jobs.FramesUntil(
        [&]
        {
            return p->Ends > 0;
        }));
    for (int i = 0; i < 5; ++i)
        f.Jobs.Frame();
    EXPECT_EQ(p->Ends, 1);
    EXPECT_EQ(p->Publishes, 0);
    EXPECT_FALSE(controlled) << "the ended run refuses every control from its end";
    EXPECT_NE(restarted, 0u);
    EXPECT_TRUE(p->Alive.expired()) << "nothing of the owner outlives the end";
    ASSERT_TRUE(f.Jobs.FramesUntil(
        [&]
        {
            return next->Ends > 0 || f.Observe(restarted).End != R::StepRunEnd::None;
        }));
}

// T4: an owner's Discard that settles the run again from inside (an owner-driven run's Recording(false), as a GPU
// transaction does) leaves Finished to the outer settle: it runs once, after Discard returned. Mutation: without the
// nesting guard it runs inside Discard.
TEST(StepRuns, FinishedRunsAfterADiscardThatSettlesTheRunAgain)
{
    Fixture f;
    R::StepRunDriver driver;
    bool insideDiscard = false, finishedInsideDiscard = false;
    int finished = 0;
    std::string refusal;
    const auto id = f.Runs.Start({.Label = "owner-driven", .Owner = "test"},
        {.Commit =
                [](std::string&)
            {
                return R::StepRunCommit::Committed;
            },
            .Discard =
                [&]
            {
                insideDiscard = true;
                driver.Recording(false); // a nested settle
                insideDiscard = false;
            },
            .Finished =
                [&](const R::StepRunObservation&)
            {
                ++finished;
                finishedInsideDiscard |= insideDiscard;
            }},
        f.Jobs.Bind(),
        refusal);
    ASSERT_NE(id, 0u) << refusal;
    driver = f.Runs.Driver(id);
    driver.Recording(true);
    ASSERT_TRUE(f.Jobs.Cancel({f.Jobs.Holders.front()})); // the holder finishes first, then the run ends
    ASSERT_TRUE(f.Jobs.FramesUntil(
        [&]
        {
            return finished > 0;
        }));
    for (int i = 0; i < 5; ++i)
        f.Jobs.Frame();
    EXPECT_EQ(finished, 1);
    EXPECT_FALSE(finishedInsideDiscard);
    EXPECT_EQ(f.Observe(id).Phase, R::StepRunPhase::Discarded);
}

// T5: Abandon (also of a retired run whose holder is still pending) and a refused Start never call Finished, and drop
// it. Mutation: an Abandon that keeps Finished calls it once the holder finishes.
TEST(StepRuns, AnAbandonedOrRefusedRunNeverCallsFinished)
{
    const auto start = [](Fixture& f, int& finished, std::weak_ptr<int>& alive, R::StepRunJobs jobs)
    {
        auto owner = std::make_shared<int>(0);
        alive = owner;
        std::string refusal;
        return f.Runs.Start({.Label = "owner-driven", .Owner = "test"},
            {.Commit =
                    [](std::string&)
                {
                    return R::StepRunCommit::Committed;
                },
                .Finished =
                    [&finished, owner](const R::StepRunObservation&)
                {
                    ++finished;
                }},
            std::move(jobs),
            refusal);
    };
    {
        Fixture f;
        int finished = 0;
        std::weak_ptr<int> alive;
        const auto id = start(f, finished, alive, f.Jobs.Bind());
        ASSERT_NE(id, 0u);
        f.Runs.Driver(id).Abandon();
        ASSERT_TRUE(f.Jobs.FramesUntil(
            [&]
            {
                return AllJobsTerminal(f.Jobs);
            }));
        EXPECT_EQ(finished, 0);
        EXPECT_TRUE(alive.expired());
        EXPECT_TRUE(AllJobsTerminal(f.Jobs));
    }
    {
        Fixture f;
        int finished = 0;
        std::weak_ptr<int> alive;
        const auto id = start(f, finished, alive, f.Jobs.Bind());
        ASSERT_NE(id, 0u);
        ASSERT_TRUE(f.Control(id, R::StepRunAction::Discard));
        f.Jobs.Frame(); // the pump retires it and cancels its holder, which finishes in a later drain
        ASSERT_TRUE(f.Jobs.Active(f.Jobs.Holders.front()));
        f.Runs.Driver(id).Abandon();
        ASSERT_TRUE(f.Jobs.FramesUntil(
            [&]
            {
                return AllJobsTerminal(f.Jobs);
            }));
        EXPECT_FALSE(f.Jobs.Active(f.Jobs.Holders.front()));
        EXPECT_EQ(finished, 0);
        EXPECT_TRUE(alive.expired());
    }
    {
        Fixture f;
        int finished = 0;
        std::weak_ptr<int> alive;
        const auto id = start(f,
            finished,
            alive,
            {.Submit =
                    [](R::PluginJob, std::string* refusal)
                {
                    *refusal = "busy";
                    return R::PluginJobHandle{};
                },
                .Cancel =
                    [](R::PluginJobHandle)
                {
                    return false;
                }});
        EXPECT_EQ(id, 0u);
        EXPECT_EQ(finished, 0);
        EXPECT_TRUE(alive.expired()) << "a refused start keeps nothing of the owner";
    }
}
