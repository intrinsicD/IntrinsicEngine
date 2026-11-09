module;

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

module Extrinsic.Runtime.StepRuns;

import Extrinsic.Core.TripleBuffer;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.ServiceRegistry;

namespace Extrinsic::Runtime
{
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;

    std::string_view ToString(const StepRunPhase phase) noexcept
    {
        switch (phase)
        {
        case StepRunPhase::Ready: return "ready";
        case StepRunPhase::Running: return "running";
        case StepRunPhase::Paused: return "paused";
        case StepRunPhase::Accepting: return "accepting";
        case StepRunPhase::Committed: return "committed";
        case StepRunPhase::Discarded: return "discarded";
        case StepRunPhase::Failed: return "failed";
        }
        return "unknown";
    }

    std::string_view ToString(const StepRunEnd end) noexcept
    {
        switch (end)
        {
        case StepRunEnd::None: return "none";
        case StepRunEnd::Converged: return "converged";
        case StepRunEnd::IterationCap: return "iteration_cap";
        case StepRunEnd::Stopped: return "stopped";
        }
        return "unknown";
    }

    std::optional<StepRunAction> ParseStepRunAction(const std::string_view name) noexcept
    {
        constexpr std::array<std::pair<std::string_view, StepRunAction>, 7> kActions{{{"run", StepRunAction::Run},
            {"pause", StepRunAction::Pause},
            {"step", StepRunAction::Step},
            {"stop", StepRunAction::Stop},
            {"accept", StepRunAction::Accept},
            {"discard", StepRunAction::Discard},
            {"restart", StepRunAction::Restart}}};
        for (const auto& [text, action] : kActions)
            if (text == name) return action;
        return std::nullopt;
    }

    std::string_view UncommittedStatus(const StepRunObservation& run) noexcept
    {
        if (run.Diagnostic.starts_with("stale")) // also a Commit refused at the holder's final check
            return "stale";
        if (run.Phase == StepRunPhase::Failed)
            return "failed";
        return run.Diagnostic == "discarded" ? "discarded" : "cancelled";
    }

    StepRunJobs PluginStepRunJobs(std::shared_ptr<PluginJobHandle> holder)
    {
        return {.Submit =
                    [holder = std::move(holder)](PluginJob job, std::string* refusal)
            {
                const bool first = !job.Run.IsValid();
                const auto handle = SubmitPluginJob({}, std::move(job), refusal);
                if (holder && first)
                    *holder = handle;
                return handle;
            },
            .Cancel =
                [](const PluginJobHandle job)
            {
                return CancelPluginJob(job);
            }};
    }

    struct StepRun
    {
        std::uint64_t Id{0u};
        StepRunOptions Options{};
        StepRunCallbacks Callbacks{};
        StepRunJobs Jobs{};
        PluginJobHandle Holder{};

        // Main thread.
        StepRunPhase Phase{StepRunPhase::Ready};
        StepRunEnd End{StepRunEnd::None};
        std::string Diagnostic{};
        bool Computing{false};                    // run or step n requested and not yet done
        std::optional<std::uint32_t> Remaining{}; // steps left of a step n (none: until paused or ended)
        bool SegmentInFlight{false};
        bool OwnerRecording{false}; // the owner's work of this run is submitted and not consumed
        bool AcceptRequested{false};
        bool Publishing{false};     // a pending Commit's publication is under way
        std::optional<bool> CommitOutcome{}; // latched once Commit answered (committed or refused)
        std::string CommitDiagnostic{};
        bool HolderFinished{false};
        bool Retired{false}; // terminal and nothing in flight: the owner's callables and the previews are gone
        bool StepFailed{false}; // a step itself answered Failed
        // StepRunCallbacks::Finished, kept apart from Callbacks (retirement clears those first) until it is called.
        std::function<void(const StepRunObservation&)> Finished{};
        int DiscardDepth{0}; // > 0 while the owner's Discard runs: a nested Settle leaves Finished to the outer one
        std::function<bool(std::string&)> PendingRestart{};
        bool RestartRunning{}; // capture control intent before an older completion changes Computing
        std::uint64_t Generation{0u};
        std::optional<std::uint32_t> ShownStep{};
        bool Unshown{false}; // the copy the reader holds is to be offered again (Show answered Later)

        // Written by the segment on a worker, read on the main thread after its completion (the job service orders
        // both), or observed while it runs (atomics).
        std::shared_ptr<Core::AppendLog<StepRunMetric>> Log{};
        std::atomic<std::uint32_t> Steps{0u};
        std::atomic<bool> Yield{false}; // stop after the step in flight
        std::atomic<std::uint64_t> Copies{0u};
        StepOutcome SegmentOutcome{StepOutcome::Continue};
        StepRunEnd SegmentEnd{StepRunEnd::None};
        std::uint32_t SegmentSteps{0u};
        // The preview: written by whoever runs the owner's code (a segment, or the pump while none runs).
        std::optional<Core::TripleBuffer<StepPreview>> Previews{std::in_place};
        std::uint32_t CopiedStep{0u};
        std::optional<Clock::time_point> LastCopy{};

        [[nodiscard]] Clock::time_point Now() const { return Options.Clock ? Options.Clock() : Clock::now(); }
        // A generation's metric log, bounded by kMaxStepRunMetricRecords (it is allocated up front).
        void NewLog()
        {
            Log = std::make_shared<Core::AppendLog<StepRunMetric>>(
                std::min(Options.Execution.IterationCap, kMaxStepRunMetricRecords));
        }
        // The live preview's time limit has passed since the last copy (or there was none).
        [[nodiscard]] bool PreviewTimeDue(const Clock::time_point now) const
        {
            return !LastCopy || now - *LastCopy >= std::chrono::milliseconds{Options.Execution.PreviewIntervalMs};
        }
        [[nodiscard]] bool InFlight() const { return SegmentInFlight || OwnerRecording; }

        // Accept on its own: convergence with auto_accept converged or end, the cap with end (a Stop never), and
        // never an older computation's while a restart waits.
        void AutoAccept()
        {
            const auto mode = Options.Execution.AutoAccept;
            const bool converged = End == StepRunEnd::Converged;
            if (!PendingRestart &&
                ((converged && mode != StepRunAutoAccept::Never) ||
                    (mode == StepRunAutoAccept::End && End == StepRunEnd::IterationCap)))
                AcceptRequested = true;
        }

        void EndAtCap()
        {
            if (Computing && End == StepRunEnd::None &&
                Steps.load(std::memory_order_relaxed) >= Options.Execution.IterationCap)
            {
                Computing = false;
                Phase = StepRunPhase::Paused;
                End = StepRunEnd::IterationCap;
                AutoAccept();
            }
        }

        // Live preview: on, and while computing (not `resting`) on its step cadence.
        [[nodiscard]] bool PreviewDue(const std::uint32_t steps, const bool resting) const
        {
            const std::uint32_t every = Options.Execution.PreviewEvery;
            return every != 0u && (resting || steps % every == 0u);
        }

        // A copy needs a Preview callable, live preview due, a newer step, the reader to have taken the previous copy
        // and the interval passed. A copy the owner could not make now (no free device slot) is not published or
        // counted; the next step tries again.
        void MaybeCopy(const bool resting)
        {
            const std::uint32_t steps = Steps.load(std::memory_order_relaxed);
            if (!Callbacks.Preview || !PreviewDue(steps, resting) || steps == CopiedStep || Previews->Unread())
                return;
            const auto now = Now();
            if (!PreviewTimeDue(now))
                return;
            StepPreview& back = Previews->Back();
            back.Generation = Generation;
            back.Step = steps;
            back.Fence = 0u;
            if (!Callbacks.Preview(back))
                return;
            Previews->Publish();
            LastCopy = now;
            CopiedStep = steps;
            Copies.fetch_add(1u, std::memory_order_relaxed);
        }

        // Worker: steps until paused, the budget is spent, the run ends or is cancelled.
        void RunSegment(const std::uint32_t budget, const std::function<bool()>& cancelled)
        {
            SegmentOutcome = StepOutcome::Continue;
            SegmentEnd = StepRunEnd::None;
            SegmentSteps = 0u;
            while (!cancelled() && !Yield.load(std::memory_order_acquire) && (budget == 0u || SegmentSteps < budget))
            {
                const std::uint32_t steps = Steps.load(std::memory_order_relaxed);
                StepRunMetric metric{.Step = steps + 1u};
                std::uint32_t completed = 1u;
                if (Callbacks.StepBatch)
                {
                    // What is left of the step n and of the cap, cut at the next live preview step.
                    const std::uint32_t cap = Options.Execution.IterationCap;
                    std::uint32_t allowance = cap > steps ? cap - steps : 1u;
                    if (budget != 0u)
                        allowance = std::min(allowance, budget - SegmentSteps);
                    if (const std::uint32_t every = Options.Execution.PreviewEvery; every != 0u && Callbacks.Preview)
                        allowance = std::min(allowance, every - steps % every);
                    allowance = std::max(allowance, 1u);
                    SegmentOutcome = Callbacks.StepBatch(metric, allowance, completed);
                    completed = std::clamp(completed, 1u, allowance);
                    metric.Step = steps + completed;
                }
                else
                    SegmentOutcome = Callbacks.Step(metric);
                SegmentSteps += completed;
                if (SegmentOutcome == StepOutcome::Failed) return;
                (void)Log->Append(metric);
                Steps.store(metric.Step, std::memory_order_relaxed);
                MaybeCopy(false);
                if (SegmentOutcome == StepOutcome::Converged) SegmentEnd = StepRunEnd::Converged;
                else if (metric.Step >= Options.Execution.IterationCap)
                    SegmentEnd = StepRunEnd::IterationCap;
                if (SegmentEnd != StepRunEnd::None) return;
            }
        }

        // Terminal at once (any caller); what needs the owner's context follows in the pump or a completion.
        void MarkTerminal(const StepRunPhase phase, std::string diagnostic)
        {
            if (!IsTerminal(Phase))
            {
                Phase = phase;
                if (!diagnostic.empty()) Diagnostic = std::move(diagnostic);
            }
            Computing = false;
            AcceptRequested = false;
            Yield.store(true, std::memory_order_release);
        }

        // The owner's context, a terminal run: what it shows goes at once (only main-thread code touches it, so it need
        // not wait for a segment, and the output is free only after it); the callables go once no segment runs; then,
        // once its holder finished too, Finished is called once. Every trigger of those conditions (a segment's or the
        // holder's completion, the owner's recording ending, a Commit's answer, the pump) reaches this.
        void Settle()
        {
            if (auto discard = std::exchange(Callbacks.Discard, {}); discard && Phase != StepRunPhase::Committed)
            {
                ++DiscardDepth; // e.g. Discard -> StepRunDriver::Recording(false) -> Settle
                discard();
                --DiscardDepth;
            }
            Callbacks.Show = {};
            if (InFlight())
                return;
            if (!Retired)
            {
                Retired = true;
                Callbacks = {};
                PendingRestart = {};
                Options.Clock = {}; // the owner's too
                Previews.reset(); // nothing reads them any more
            }
            if (HolderFinished && DiscardDepth == 0 && IsTerminal(Phase) && Finished)
                std::exchange(Finished, {})(Observation()); // from the run itself: Trim may already have erased it
        }

        // Main thread: the run as run_observe shows it.
        [[nodiscard]] StepRunObservation Observation() const
        {
            return {.Id = Id,
                .Label = Options.Label,
                .Owner = Options.Owner,
                .Diagnostic = Diagnostic,
                .MetricNames = Options.MetricNames,
                .Phase = Phase,
                .End = End,
                .Generation = Generation,
                .Steps = Steps.load(std::memory_order_relaxed),
                .SegmentInFlight = InFlight(),
                .WorkerInFlight = SegmentInFlight,
                .PreviewCopies = Copies.load(std::memory_order_relaxed),
                .CanRestart = !Retired && bool(Callbacks.Restart),
                .StepFailed = StepFailed,
                .Execution = Options.Execution,
                .DevicePreviewBuffers = Options.DevicePreview && Options.Execution.PreviewEvery != 0u
                    ? Options.Execution.PreviewBuffers
                    : 0u,
                .ShownStep = ShownStep,
                .Metrics = Log};
        }

        // In the owner's context.
        void Terminate(const StepRunPhase phase, std::string diagnostic = {})
        {
            MarkTerminal(phase, std::move(diagnostic));
            Settle();
        }

        // Latest wins: once a restart is requested, nothing of the older computation shows. A copy Show refused is
        // offered again on the next pump until it is shown or a newer one replaces it.
        void Show()
        {
            const bool newer = Previews->Update();
            // A copy of an older generation (made before a restart was applied) is never shown.
            if (PendingRestart || !Callbacks.Show || Previews->Front().Generation != Generation)
            {
                Unshown = false;
                return;
            }
            if (!newer && !Unshown) return;
            const StepRunShow shown = Callbacks.Show(Previews->Front());
            Unshown = shown == StepRunShow::Later;
            if (shown == StepRunShow::Shown) ShownStep = Previews->Front().Step;
        }

        bool Valid() const { return !Callbacks.Valid || Callbacks.Valid(); }

        void SubmitSegment(const std::shared_ptr<StepRun>& self)
        {
            const std::uint32_t budget = Remaining.value_or(0u);
            PluginJob job{.Label = Options.Label, .Run = Holder};
            job.Work = [self, budget](const std::function<bool()>& cancelled) { self->RunSegment(budget, cancelled); };
            job.Completion = [self](const bool applied) { return self->SegmentDone(applied); };
            std::string refusal;
            SegmentInFlight = true;
            if (!Jobs.Submit(std::move(job), &refusal).IsValid())
            {
                SegmentInFlight = false;
                Terminate(StepRunPhase::Failed, "the next step was refused: " + refusal);
            }
        }

        // Main thread, the segment's completion (also a late one after the run ended).
        bool SegmentDone(const bool applied)
        {
            SegmentInFlight = false;
            if (IsTerminal(Phase))
            {
                Settle();
                return applied;
            }
            if (!applied)
            {
                Terminate(StepRunPhase::Discarded, "cancelled");
                return false;
            }
            if (SegmentOutcome == StepOutcome::Failed)
            {
                StepFailed = true;
                Terminate(StepRunPhase::Failed, "a step failed");
                return true;
            }
            if (Remaining) *Remaining -= std::min(*Remaining, SegmentSteps);
            if (SegmentEnd != StepRunEnd::None && End == StepRunEnd::None) End = SegmentEnd;
            if (End != StepRunEnd::None || (Remaining && *Remaining == 0u)) Computing = false;
            if (!Computing) Phase = StepRunPhase::Paused;
            AutoAccept();
            return true;
        }

        // Main thread, the holder's readiness gate, once per drain: true starts Accept (the holder's completion).
        bool Pump(const std::shared_ptr<StepRun>& self)
        {
            if (IsTerminal(Phase))
            {
                Settle();
                if (Jobs.Cancel(Holder)) // once it took, the gate is not asked again
                    return false;
                // Refused (a detached owner's surface cannot cancel): the holder completes once nothing is in flight
                // (its completion cannot commit a terminal run), so it does not keep the output forever.
                return !InFlight();
            }
            // Accepting: the latched outcome decides, before any capture check (a publication changes the captures).
            if (Phase == StepRunPhase::Accepting) return CommitOutcome.has_value();
            if (!Valid())
            {
                Terminate(StepRunPhase::Discarded, "stale: the captured inputs or the world changed");
                return false;
            }
            Show();
            if (InFlight()) return false;
            if (PendingRestart)
            {
                auto init = std::exchange(PendingRestart, {});
                std::string diagnostic;
                const bool restarted = init(diagnostic);
                // The owner may have ended the run from its initializer (a failure): nothing more here.
                if (IsTerminal(Phase))
                    return false;
                if (restarted)
                    BeginGeneration();
                else // the owner refused: nothing changed, the run goes on as it was
                    Diagnostic = "restart refused: " + diagnostic;
            }
            Yield.store(false, std::memory_order_release);
            if (AcceptRequested)
            {
                Phase = StepRunPhase::Accepting;
                // A worker run commits in the holder's completion, after the job service's final checks; an owner's
                // Commit starts here (its publication has its own) and may answer later.
                if (Callbacks.Step) return true;
                std::string diagnostic;
                const StepRunCommit result = Callbacks.Commit(diagnostic);
                if (result == StepRunCommit::Pending) return false; // answered through the driver
                CommitOutcome = result == StepRunCommit::Committed;
                CommitDiagnostic = std::move(diagnostic);
                return true;
            }
            if (Computing && End == StepRunEnd::None)
            {
                Phase = StepRunPhase::Running;
                // No Step: the owner asks MayStart. A worker run waits for its owner's Init (Prepared).
                if (Callbacks.Step && (!Callbacks.Prepared || Callbacks.Prepared()))
                    SubmitSegment(self);
                return false;
            }
            MaybeCopy(true); // idle: the paused or ended state still shows, at the same limits
            return false;
        }

        // A new generation from step 0: computing when the run starts running or was running (not a step n), else
        // ready; the older generation's log and copies are not kept.
        void BeginGeneration()
        {
            Computing = std::exchange(RestartRunning, false);
            ++Generation;
            NewLog();
            Steps.store(0u, std::memory_order_relaxed);
            CopiedStep = 0u;
            LastCopy.reset();
            ShownStep.reset();
            Unshown = false;
            End = StepRunEnd::None;
            Remaining.reset();
            Phase = Computing ? StepRunPhase::Running : StepRunPhase::Ready;
            Diagnostic.clear();
            EndAtCap();
        }

        // Main thread, the holder's completion: Accept's latched outcome when applied, else the run was cancelled (but
        // a publication under way or answered decides: an unanswered one settles in CommitFinished).
        bool HolderDone(const bool applied)
        {
            HolderFinished = true;
            Jobs = {}; // only the holder's pump used them
            if (applied && Phase == StepRunPhase::Accepting && Callbacks.Step && !CommitOutcome)
            {
                std::string diagnostic;
                const StepRunCommit result = Valid() ? Callbacks.Commit(diagnostic) : StepRunCommit::Refused;
                // Started, not finished (a GPU run reads its labels back): the run stays Accepting without a pump;
                // the owner answers through CommitFinished (also after a discard won first, which settles it).
                if (result == StepRunCommit::Pending)
                    return true;
                CommitOutcome = result == StepRunCommit::Committed;
                CommitDiagnostic = std::move(diagnostic);
            }
            if (CommitOutcome) FinishCommit();
            else if (!Publishing) Terminate(StepRunPhase::Discarded, "cancelled");
            return applied && CommitOutcome.value_or(false);
        }

        // The latched Commit outcome made terminal.
        void FinishCommit()
        {
            if (CommitOutcome.value_or(false))
                Terminate(StepRunPhase::Committed);
            else
                Terminate(StepRunPhase::Failed,
                          CommitDiagnostic.empty() ? "stale: the captured inputs changed" : CommitDiagnostic);
        }
    };

    bool StepRunDriver::MayStart() const
    {
        const auto run = m_Run.lock();
        return run && !run->Retired && !IsTerminal(run->Phase) && run->Phase != StepRunPhase::Accepting && run->Computing &&
               run->End == StepRunEnd::None && !run->AcceptRequested && !run->PendingRestart;
    }

    void StepRunDriver::Recording(const bool inFlight)
    {
        const auto run = m_Run.lock();
        if (!run || run->Retired) return;
        run->OwnerRecording = inFlight;
        if (inFlight) return;
        if (IsTerminal(run->Phase))
            run->Settle(); // a retirement waited for this
        else if (!run->Computing && !run->InFlight() && run->Phase == StepRunPhase::Running)
            run->Phase = StepRunPhase::Paused;
    }

    std::uint32_t StepRunDriver::Allowance() const
    {
        if (!MayStart())
            return 0u;
        const auto run = m_Run.lock();
        const std::uint32_t steps = run->Steps.load(std::memory_order_relaxed);
        const std::uint32_t cap = run->Options.Execution.IterationCap;
        const std::uint32_t left = cap > steps ? cap - steps : 0u;
        return run->Remaining ? std::min(*run->Remaining, left) : left;
    }

    bool StepRunDriver::Stepped(StepRunMetric metric, const StepOutcome outcome, const std::uint32_t count)
    {
        const auto run = m_Run.lock();
        if (!run || run->Retired || IsTerminal(run->Phase) || run->Phase == StepRunPhase::Accepting) return false;
        auto& r = *run;
        if (outcome == StepOutcome::Failed)
        {
            r.StepFailed = true;
            r.Terminate(StepRunPhase::Failed, "a step failed");
            return false;
        }
        if (count == 0u)
            return r.Computing && r.End == StepRunEnd::None && !r.PendingRestart;
        const std::uint32_t before = r.Steps.load(std::memory_order_relaxed);
        metric.Step = before + std::min(count, std::numeric_limits<std::uint32_t>::max() - before);
        (void)r.Log->Append(metric);
        r.Steps.store(metric.Step, std::memory_order_relaxed);
        if (r.End == StepRunEnd::None) // an earlier Stop stays; convergence wins over a simultaneous cap
        {
            if (outcome == StepOutcome::Converged) r.End = StepRunEnd::Converged;
            else if (metric.Step >= r.Options.Execution.IterationCap)
                r.End = StepRunEnd::IterationCap;
        }
        if (r.Remaining)
            *r.Remaining -= std::min(*r.Remaining, count);
        if (r.End != StepRunEnd::None || (r.Remaining && *r.Remaining == 0u)) r.Computing = false;
        if (!r.Computing && !r.OwnerRecording) r.Phase = StepRunPhase::Paused;
        r.AutoAccept();
        return r.Computing && r.End == StepRunEnd::None && !r.PendingRestart;
    }

    bool StepRunDriver::MayPreview() const
    {
        const auto run = m_Run.lock();
        return run && !run->Retired && !IsTerminal(run->Phase) && !run->PendingRestart &&
            run->PreviewDue(run->Steps.load(std::memory_order_relaxed), !run->Computing) &&
            run->PreviewTimeDue(run->Now());
    }

    bool StepRunDriver::MayPreviewAt(const std::uint32_t step) const
    {
        const auto run = m_Run.lock();
        return run && !run->Retired && !IsTerminal(run->Phase) && !run->PendingRestart &&
            run->PreviewDue(step, false) && run->PreviewTimeDue(run->Now());
    }

    void StepRunDriver::Previewed()
    {
        const auto run = m_Run.lock();
        if (!run || run->Retired)
            return;
        run->LastCopy = run->Now();
        run->Copies.fetch_add(1u, std::memory_order_relaxed);
    }

    bool StepRunDriver::MayCommit() const
    {
        const auto run = m_Run.lock();
        return run && !run->Retired && !IsTerminal(run->Phase) && !run->PendingRestart &&
            (run->Phase == StepRunPhase::Accepting || (!run->Computing && !run->InFlight()));
    }

    bool StepRunDriver::Committing()
    {
        const auto run = m_Run.lock();
        if (!run || run->Retired || IsTerminal(run->Phase))
            return false;
        if (run->Phase == StepRunPhase::Accepting)
            return true;
        if (run->Computing || run->InFlight() || run->PendingRestart)
            return false;
        run->AcceptRequested = false;
        run->Phase = StepRunPhase::Accepting; // the holder's pump now waits for CommitFinished
        return true;
    }

    bool StepRunDriver::Publishing()
    {
        const auto run = m_Run.lock();
        if (!run)
            return !m_Bound; // a bound run that is gone ended long ago
        if (run->Retired || IsTerminal(run->Phase))
            return false;
        if (run->Phase == StepRunPhase::Accepting)
            run->Publishing = true;
        return true;
    }

    void StepRunDriver::CommitFinished(const bool committed, std::string diagnostic)
    {
        const auto run = m_Run.lock();
        if (!run || run->Retired)
            return;
        // Late after a discard (terminal already) or a second answer: ignored, except that a run whose holder ended
        // (a worker run's pending Commit) has no pump left: the answer settles it here, in the owner's context.
        if (IsTerminal(run->Phase))
        {
            if (run->HolderFinished)
                run->Settle();
            return;
        }
        if (run->Phase != StepRunPhase::Accepting || run->CommitOutcome)
            return;
        run->CommitOutcome = committed;
        run->CommitDiagnostic = std::move(diagnostic);
        if (run->HolderFinished) run->FinishCommit(); // no pump left: settle here
    }

    void StepRunDriver::Abandon()
    {
        const auto run = m_Run.lock();
        if (!run)
            return;
        run->Finished = {}; // never called (also of a retired run whose holder is still pending): the owner goes away
        if (run->Retired)
            return;
        run->OwnerRecording = false;
        run->MarkTerminal(StepRunPhase::Discarded, "its owner shut down");
        run->Settle();
        if (run->Retired)
            return;
        // A worker segment still runs: it reads only Step, StepBatch, Preview and the clock, which go once it
        // returned (its completion settles); the main-thread callables go now.
        auto& callbacks = run->Callbacks;
        callbacks.Valid = {};
        callbacks.Commit = {};
        callbacks.Restart = {};
        callbacks.Prepared = {};
        run->PendingRestart = {};
    }

    StepRunJobs EditorStepRunJobs(
        EditorJobCommandSurface surface, EditorJobIdentity identity, std::shared_ptr<JobToken> holder)
    {
        auto retiring = surface; // the run's own retirement is not a cancel request
        if (surface.Retire)
            retiring.Cancel = surface.Retire;
        return {.Submit = [surface, identity, holder](PluginJob job, std::string*) -> PluginJobHandle
            {
                // The holder keeps the owner's identity (a run it joins, if any); later jobs join the holder.
                const bool holderJob = !job.Run.IsValid();
                auto id = identity;
                if (!holderJob)
                    id.Run = job.Run.Token;
                auto held = std::make_shared<PluginJob>(std::move(job));
                auto finalized = std::make_shared<bool>(false);
                JobDesc desc{};
                desc.DebugName = held->Label;
                desc.Work = [held](const JobCancellation& cancellation)
                {
                    held->Work(
                        [&cancellation]
                        {
                            return cancellation.IsCancelled();
                        });
                    return JobResultEnvelope::Make(true);
                };
                if (held->IsReadyToApply)
                    desc.IsReadyToApply = [held]
                    {
                        return held->IsReadyToApply();
                    };
                desc.PublishCompletion = [held, finalized](KernelEventBus&, const JobResultEnvelope&)
                {
                    *finalized = true;
                    return held->Completion(true);
                };
                desc.FinalizeUnpublishedOnMainThread = [held, finalized]
                {
                    if (!std::exchange(*finalized, true))
                        (void)held->Completion(false);
                };
                const JobToken token = surface.Submit(std::move(desc), std::move(id));
                if (holderJob)
                    *holder = token;
                return {token};
            },
            // Without a job listing (a module's own job lane) only the job itself: an owner-driven run's holder
            // is its only job.
            .Cancel =
                [retiring = std::move(retiring)](const PluginJobHandle job)
            {
                const auto count = CancelEditorRuns(retiring, std::span<const JobToken>{&job.Token, 1u});
                if (!count.Unavailable)
                    return count.Requested > 0u;
                return retiring.Cancel && retiring.Cancel(job.Token) == EditorJobCancelStatus::Requested;
            }};
    }

    extern "C++"
    {
        StepRunsService::StepRunsService() = default;
        StepRunsService::~StepRunsService() = default;

        std::uint64_t StepRunsService::Start(
            StepRunOptions options, StepRunCallbacks callbacks, StepRunJobs jobs, std::string& refusal)
        {
            if (!callbacks.Commit || !jobs.Submit || !jobs.Cancel)
            {
                refusal = "a step run needs Commit and the owner's Submit and Cancel";
                return 0u;
            }
            if (const auto invalid =
                    StepRunExecutionRefusal(options.Execution, std::numeric_limits<std::uint32_t>::max(), 0u);
                !invalid.empty())
            {
                refusal = std::string{invalid};
                return 0u;
            }
            auto run = std::make_shared<StepRun>();
            run->Id = m_NextId;
            run->Options = std::move(options);
            run->Finished = std::exchange(callbacks.Finished, {});
            run->Callbacks = std::move(callbacks);
            run->Jobs = std::move(jobs);
            run->NewLog();
            if (run->Options.Execution.Start == StepRunStart::Run)
            {
                run->Computing = true; // as a run control right after the start
                run->Phase = StepRunPhase::Running;
            }
            run->EndAtCap();
            PluginJob holder{.Label = run->Options.Label, .Output = run->Options.Output};
            holder.Work = [](const std::function<bool()>&) {};
            holder.IsReadyToApply = [run]
            {
                return run->Pump(run);
            };
            holder.Completion = [run](const bool applied)
            {
                return run->HolderDone(applied);
            };
            run->Holder = run->Jobs.Submit(std::move(holder), &refusal);
            if (!run->Holder.IsValid())
            {
                run->Callbacks = {};
                if (refusal.empty())
                    refusal = "the owner refused the run's job";
                return 0u;
            }
            m_Runs.emplace(m_NextId, run);
            Trim();
            return m_NextId++;
        }

        void StepRunsService::Trim()
        {
            constexpr std::ptrdiff_t kKeptRetired = 32;
            auto retired = std::ranges::count_if(m_Runs,
                [](const auto& entry)
                {
                    return entry.second->Retired;
                });
            for (auto it = m_Runs.begin(); it != m_Runs.end() && retired > kKeptRetired;)
                if (it->second->Retired)
                {
                    it = m_Runs.erase(it);
                    --retired;
                }
                else
                    ++it;
        }

        bool StepRunsService::Control(
            const std::uint64_t id, const StepRunAction action, const std::uint32_t count, std::string& refusal)
        {
            Trim();
            const auto found = m_Runs.find(id);
            if (found == m_Runs.end())
            {
                refusal = "no run " + std::to_string(id);
                return false;
            }
            StepRun& run = *found->second;
            const auto refuse = [&](std::string why)
            {
                refusal = std::move(why);
                return false;
            };
            if (IsTerminal(run.Phase))
                return refuse("the run is " + std::string{ToString(run.Phase)});
            if (run.Phase == StepRunPhase::Accepting)
            {
                // Before a pending Commit's publication a discard still wins; from then on the publication decides.
                if (action != StepRunAction::Discard || run.Publishing)
                    return refuse("the run is being accepted");
                run.MarkTerminal(StepRunPhase::Discarded, "discarded");
                return true;
            }
            if (run.PendingRestart && (action == StepRunAction::Run || action == StepRunAction::Step))
                return refuse("the run is restarting; wait for its new generation");
            switch (action)
            {
            case StepRunAction::Run:
            case StepRunAction::Step:
                if (run.End != StepRunEnd::None)
                    return refuse("the run ended (" + std::string{ToString(run.End)} + "); Accept or Discard");
                if (action == StepRunAction::Step && count == 0u)
                    return refuse("step needs a count of at least 1");
                if (action == StepRunAction::Step && run.InFlight())
                    return refuse("the run is computing; pause first");
                run.Computing = true;
                run.Remaining = action == StepRunAction::Step ? std::optional{count} : std::nullopt;
                run.Phase = StepRunPhase::Running;
                run.EndAtCap();
                return true;
            case StepRunAction::Pause:
                if (!run.Computing && !(run.PendingRestart && run.RestartRunning))
                    return refuse("the run is not computing");
                run.RestartRunning = false;
                run.Computing = false;
                run.Yield.store(true, std::memory_order_release);
                if (!run.InFlight())
                    run.Phase = StepRunPhase::Paused;
                return true;
            case StepRunAction::Stop:
                if (run.End == StepRunEnd::None)
                    run.End = StepRunEnd::Stopped;
                run.RestartRunning = false;
                run.Computing = false;
                run.Yield.store(true, std::memory_order_release);
                if (!run.InFlight())
                    run.Phase = StepRunPhase::Paused;
                return true;
            case StepRunAction::Accept:
                if (run.Computing || run.InFlight() || run.PendingRestart)
                    return refuse("pause or stop the run first");
                if (run.Steps.load(std::memory_order_relaxed) == 0u && run.End != StepRunEnd::IterationCap)
                    return refuse("the run has no step yet");
                if (run.AcceptRequested)
                    return refuse("Accept is already requested");
                run.AcceptRequested = true;
                return true;
            case StepRunAction::Discard: // the pump retires it and cancels its jobs, in the owner's context
                run.MarkTerminal(StepRunPhase::Discarded, "discarded");
                return true;
            case StepRunAction::Restart:
                if (!run.Callbacks.Restart)
                    return refuse("this run cannot restart (its owner gives no initializer); discard it and start "
                                  "a new one");
                return Restart(id, run.Callbacks.Restart, refusal);
            }
            return refuse("unknown action");
        }

        StepRunDriver StepRunsService::Driver(const std::uint64_t id)
        {
            const auto found = m_Runs.find(id);
            return found == m_Runs.end() ? StepRunDriver{} : StepRunDriver{found->second};
        }

        bool StepRunsService::Restart(
            const std::uint64_t id, std::function<bool(std::string&)> init, std::string& refusal)
        {
            Trim();
            const auto found = m_Runs.find(id);
            if (found == m_Runs.end() || !init)
            {
                refusal = found == m_Runs.end() ? "no run " + std::to_string(id) : "a restart needs its init";
                return false;
            }
            StepRun& run = *found->second;
            if (IsTerminal(run.Phase) || run.Phase == StepRunPhase::Accepting)
            {
                refusal = "the run is " + std::string{ToString(run.Phase)};
                return false;
            }
            run.RestartRunning = run.Options.Execution.Start == StepRunStart::Run ||
                (run.PendingRestart ? run.RestartRunning : run.Computing && !run.Remaining);
            run.PendingRestart = std::move(init); // an older pending restart is dropped, never called
            run.AcceptRequested = false;
            run.Yield.store(true, std::memory_order_release);
            return true;
        }

        std::optional<StepRunObservation> StepRunsService::Observe(const std::uint64_t id)
        {
            Trim();
            const auto found = m_Runs.find(id);
            if (found == m_Runs.end())
                return std::nullopt;
            return found->second->Observation();
        }

        std::vector<std::uint64_t> StepRunsService::Runs()
        {
            Trim();
            std::vector<std::uint64_t> ids;
            for (const auto& [id, run] : m_Runs)
                ids.push_back(id);
            return ids;
        }

        namespace
        {
            AgentOperationOutcome Fail(std::string code, std::string message)
            {
                return {.IsError = true, .Text = std::move(message), .ErrorCode = std::move(code)};
            }

            Json ToJson(const StepRunObservation& run, const bool allMetrics)
            {
                const auto records = run.Metrics ? run.Metrics->Records() : std::span<const StepRunMetric>{};
                const auto metric = [&](const StepRunMetric& m)
                {
                    Json values = Json::object();
                    for (std::size_t i = 0; i < m.Values.size(); ++i)
                        if (!run.MetricNames[i].empty())
                            values[run.MetricNames[i]] = m.Values[i];
                    return Json{{"step", m.Step}, {"values", values}};
                };
                Json out{{"run", run.Id},
                    {"label", run.Label},
                    {"owner", run.Owner},
                    {"phase", ToString(run.Phase)},
                    {"end", ToString(run.End)},
                    {"diagnostic", run.Diagnostic},
                    {"generation", run.Generation},
                    {"steps", run.Steps},
                    {"computing", run.SegmentInFlight},
                    {"preview_copies", run.PreviewCopies},
                    {"device_preview_buffers", run.DevicePreviewBuffers},
                    {"can_restart", run.CanRestart},
                    {"execution",
                        {{"max_iterations", run.Execution.IterationCap},
                            {"preview_every", run.Execution.PreviewEvery},
                            {"preview_interval_ms", run.Execution.PreviewIntervalMs},
                            {"preview_buffers", run.Execution.PreviewBuffers},
                            {"auto_accept", ToString(run.Execution.AutoAccept)},
                            {"start", ToString(run.Execution.Start)}}},
                    {"shown_step", run.ShownStep ? Json(*run.ShownStep) : Json(nullptr)},
                    {"last_metric", records.empty() ? Json(nullptr) : metric(records.back())}};
                if (allMetrics)
                {
                    Json all = Json::array();
                    for (const auto& m : records)
                        all.push_back(metric(m));
                    out["metrics"] = std::move(all);
                }
                return out;
            }
        }

        void StepRunsService::RegisterOperations(AgentOperationRegistry& registry)
        {
            constexpr const char* kRun =
                R"("run":{"type":"integer","minimum":1,"description":"The run's id (run_observe without it lists the runs)."})";
            (void)registry.Register({
                .Name = "run_control",
                .Title = "Control a step run",
                .Description =
                    "run (until paused or ended), pause (after the step in flight), step (count steps), stop "
                    "(pause and end; a stopped run waits for accept or discard, it never accepts on its own), "
                    "accept (commit once, from a paused or ended run), discard (end without committing), restart "
                    "(once nothing is in flight the owner re-initializes: a new generation from step 0, running "
                    "when the run starts running or was running, else ready; refused when run_observe's "
                    "can_restart is false). Refused once the run is committed, discarded or failed. Takes effect "
                    "from the next frame; run_observe shows it.",
                .InputSchemaJson = std::string{R"({"type":"object","properties":{)"} + kRun +
                    R"(,"action":{"type":"string","enum":["run","pause","step","stop","accept","discard","restart"]},)"
                    R"("count":{"type":"integer","minimum":1,"maximum":4294967295}},"required":["run","action"],"additionalProperties":false})",
                .ReadOnly = false,
                .Invoke =
                    [this](const AgentOperationContext&, const std::string_view arguments)
                {
                    // The dispatcher validated the arguments against the schema (run and action required, count 1..2^32-1).
                    const Json args = Json::parse(arguments, nullptr, false);
                    const auto action = ParseStepRunAction(args.value("action", ""));
                    if (!action)
                        return Fail("invalid_arguments", "unknown action");
                    std::string refusal;
                    const auto id = args.value("run", std::uint64_t{0});
                    if (!Control(
                            id, *action, static_cast<std::uint32_t>(args.value("count", std::uint64_t{1})), refusal))
                        return Fail("refused", refusal);
                    return AgentOperationOutcome{.Text = ToJson(*Observe(id), false).dump()};
                },
            });
            (void)registry.Register({
                .Name = "run_observe",
                .Title = "Observe step runs",
                .Description = "A run's phase, end reason, step count, generation (restarts), preview copies, the step "
                               "shown last and its last metric; with metrics, every metric of this generation. Without "
                               "run, the ids of the runs.",
                .InputSchemaJson = std::string{R"({"type":"object","properties":{)"} + kRun +
                    R"(,"metrics":{"type":"boolean"}},"additionalProperties":false})",
                .Invoke =
                    [this](const AgentOperationContext&, const std::string_view arguments)
                {
                    const Json args = Json::parse(arguments, nullptr, false); // an object (the dispatcher validated it)
                    if (!args.contains("run"))
                        return AgentOperationOutcome{.Text = Json{{"runs", Runs()}}.dump()};
                    const auto run = Observe(args.value("run", std::uint64_t{0}));
                    if (!run)
                        return Fail("not_found", "no such run");
                    return AgentOperationOutcome{.Text = ToJson(*run, args.value("metrics", false)).dump()};
                },
            });
        }
    }

    std::uint64_t StartOneShotWorkerRun(StepRunsService& runs,
        StepRunOptions options,
        OneShotWorkerCallbacks owner,
        StepRunJobs jobs,
        std::string& refusal)
    {
        if (!owner.Compute || !owner.Publish || !owner.Ended)
        {
            refusal = "a one-shot run needs Compute, Publish and Ended";
            return 0u;
        }
        // The callables below hold it until the run retires (Finished until it is called); Answered: Publish or Ended
        // was called (once).
        struct State
        {
            OneShotWorkerCallbacks Owner{};
            bool Answered{false};
        };
        const auto s = std::make_shared<State>(State{.Owner = std::move(owner)});
        options.Execution = OneShotExecution(options.Execution);
        StepRunCallbacks callbacks{.Step =
                                       [s](StepRunMetric& metric)
            {
                if (!s->Owner.Compute())
                    return StepOutcome::Failed;
                if (s->Owner.Metric)
                    s->Owner.Metric(metric);
                return StepOutcome::Converged;
            },
            .Commit =
                [s](std::string& diagnostic)
            {
                s->Answered = true;
                return s->Owner.Publish(diagnostic) ? StepRunCommit::Committed : StepRunCommit::Refused;
            },
            // Ended waits for Finished: the step may still run, and the holder still holds the output.
            .Discard =
                [s]
            {
                if (s->Owner.Abort)
                    s->Owner.Abort();
            },
            .Restart =
                [s](std::string&)
            {
                if (s->Owner.Restart)
                    s->Owner.Restart();
                return true;
            },
            .Finished =
                [s](const StepRunObservation& run)
            {
                if (!std::exchange(s->Answered, true))
                    s->Owner.Ended(run);
            }};
        if (s->Owner.Valid)
            callbacks.Valid = [s]
            {
                return s->Owner.Valid();
            };
        return runs.Start(std::move(options), std::move(callbacks), std::move(jobs), refusal);
    }

    RuntimeModuleResult StepRunsModule::OnRegister(EngineSetup& setup)
    {
        return setup.Services().Provide<StepRunsService>(m_Service, Name());
    }
}
