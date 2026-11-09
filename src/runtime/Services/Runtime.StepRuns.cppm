// ARCH-022 S3: step runs, the job form of iterative work with preview and accept. A run steps on a worker, one
// `Step` at a time, checks its controls between steps (run, pause, step n, stop, accept, discard), records one metric
// per step, hands its newest state to the main thread at a limited rate, and commits once on Accept. It is carried by
// jobs of its owner (a plugin through its contract, or a runtime module): a holder job that holds the output until
// the run ends (admission, a Jobs row, cancel and an unload drain all see it; its readiness gate is the run's
// main-thread pump) and segment jobs joined to it (the worker loop).
//
// An owner-driven run (no `Step`: the owner's iterations advance in its own main-thread maintenance, as resident GPU
// work does) is told what happens through its StepRunDriver; there "in flight" is the owner's `Recording`.
//
//   ready --run/step n--> running --pause, steps done--> paused --run/step n--> running
//   ready --run/step n with cap 0--> paused (iteration cap; initialized state can be accepted)
//   running --converged, iteration cap, stop--> paused (ended: Accept, Discard or Restart only)
//   paused (also ended) --accept--> accepting --> committed | failed (Commit refused, e.g. stale)
//   converged with auto_accept converged or end, or capped with end --> accepting at once (a Stop never accepts)
//   ready, running or paused --restart (the owner's Restart initializer, once nothing is in flight)--> a new
//   generation from step 0, running when the run starts running or was running (not a step n), else ready; a
//   refused initializer leaves the run as it was
//   accepting (Commit pending) --publishing--> only its end decides; before it a discard or cancel still wins
//   (a worker run's Commit may also answer Pending: its holder ends, the run stays accepting until CommitFinished)
//   any --discard, cancel (Jobs window, unload), invalid captures--> discarded;  a failing step --> failed
// Terminal phases refuse every control. The owner's `Discard` runs once in its context as soon as the run is terminal;
// every callable of the owner is dropped once nothing is in flight; its optional `Finished` runs once after that and
// after the holder job finished (the output is free again: the settled end).
module;

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Extrinsic.Runtime.StepRuns;

import Extrinsic.Core.AppendLog;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.ModuleLifecycle;
export import Extrinsic.Runtime.PluginContract;
export import Extrinsic.Runtime.StepRunExecution; // the shared execution options

export namespace Extrinsic::Runtime
{
    enum class StepRunPhase : std::uint8_t { Ready, Running, Paused, Accepting, Committed, Discarded, Failed };
    enum class StepRunEnd : std::uint8_t { None, Converged, IterationCap, Stopped };
    enum class StepOutcome : std::uint8_t { Continue, Converged, Failed };
    enum class StepRunAction : std::uint8_t
    {
        Run,
        Pause,
        Step,
        Stop,
        Accept,
        Discard,
        Restart
    };
    // What Show did with a copy: shown; not now (offered again until shown or replaced); cannot be shown (dropped).
    enum class StepRunShow : std::uint8_t { Shown, Later, Never };
    // What Commit did: published, refused (stale, say), or started and answered later through StepRunDriver.
    enum class StepRunCommit : std::uint8_t { Committed, Refused, Pending };
    [[nodiscard]] std::string_view ToString(StepRunPhase phase) noexcept;
    [[nodiscard]] std::string_view ToString(StepRunEnd end) noexcept;
    [[nodiscard]] std::optional<StepRunAction> ParseStepRunAction(std::string_view name) noexcept;
    [[nodiscard]] constexpr bool IsTerminal(const StepRunPhase phase) noexcept
    {
        return phase == StepRunPhase::Committed || phase == StepRunPhase::Discarded || phase == StepRunPhase::Failed;
    }

    // The metric log of a generation holds at most this many records (it is allocated up front): a run with a larger
    // cap (one step per sample, say) logs its first ones and still counts every step.
    inline constexpr std::uint32_t kMaxStepRunMetricRecords = 1u << 16;

    // One step's record: its number (1-based within a generation) and up to four values, named by the run.
    struct StepRunMetric
    {
        std::uint32_t Step{0u};
        std::array<double, 4> Values{};
    };

    // A copy of the run's state for display; Generation counts restarts. A copy recorded on the device (a GPU
    // method's preview slot) names its completion in Fence (an RHI::ReadbackToken value; 0: a host copy, complete
    // when made): Show answers Later until it completed.
    struct StepPreview
    {
        std::uint64_t Generation{0u};
        std::uint32_t Step{0u};
        std::vector<float> Values{};
        std::uint64_t Fence{0u};
    };

    // ARCH-023: the hand-off of one device preview slot between a GPU run's owner and its worker step, never
    // blocking either. The owner (main thread) Offers a slot it acquired; the worker's Preview Borrows it (false:
    // none offered, so no copy is made or counted), records its copy into the slot and Records the copy's fence; the
    // owner's Show Takes the recorded slot once its fence completed and publishes it. The slot (and any lease in it)
    // never leaves the mailbox on the worker: only the owner offers, takes or Clears it, and Clear refuses while the
    // worker borrows (the owner then waits for the step to end). One slot at a time: a busy or unpublished slot
    // drops the next preview instead of stalling the computation.
    template <class Slot> class StepPreviewMailbox
    {
    public:
        // Owner: offers `slot` when the mailbox is empty; false (slot not taken) otherwise.
        bool Offer(Slot slot)
        {
            const std::scoped_lock lock{m_Mutex};
            if (m_State != State::Empty)
                return false;
            m_Slot = std::move(slot);
            m_State = State::Offered;
            return true;
        }
        [[nodiscard]] bool Empty() const
        {
            const std::scoped_lock lock{m_Mutex};
            return m_State == State::Empty;
        }
        // Worker (or the owner at a rest): borrows the offered slot, `strip` copying what recording needs (never the
        // lease); false when none is offered.
        template <class Strip> bool Borrow(Strip&& strip)
        {
            const std::scoped_lock lock{m_Mutex};
            if (m_State != State::Offered)
                return false;
            strip(*m_Slot);
            m_State = State::Borrowed;
            return true;
        }
        // Worker: the borrowed slot's copy was submitted with `fence` (0: nothing submitted, the slot is offered
        // again).
        void Recorded(const std::uint64_t fence)
        {
            const std::scoped_lock lock{m_Mutex};
            if (m_State != State::Borrowed)
                return;
            m_Fence = fence;
            m_State = fence != 0u ? State::Recorded : State::Offered;
        }
        // Owner: the slot whose copy `fence` names, emptying the mailbox; nullopt for any other state or fence.
        [[nodiscard]] std::optional<Slot> Take(const std::uint64_t fence)
        {
            const std::scoped_lock lock{m_Mutex};
            if (m_State != State::Recorded || m_Fence != fence)
                return std::nullopt;
            m_State = State::Empty;
            return std::exchange(m_Slot, std::nullopt);
        }
        // Owner: empties the mailbox, returning its slot (with the fence of a recorded copy, 0 otherwise) for the
        // owner to stamp and release; nullopt when empty or while the worker borrows it.
        [[nodiscard]] std::optional<std::pair<Slot, std::uint64_t>> Clear()
        {
            const std::scoped_lock lock{m_Mutex};
            if (m_State == State::Empty || m_State == State::Borrowed)
                return std::nullopt;
            const std::uint64_t fence = m_State == State::Recorded ? m_Fence : 0u;
            m_State = State::Empty;
            return std::pair{*std::exchange(m_Slot, std::nullopt), fence};
        }

    private:
        enum class State : std::uint8_t
        {
            Empty,
            Offered,
            Borrowed,
            Recorded
        };
        mutable std::mutex m_Mutex{};
        std::optional<Slot> m_Slot{};
        std::uint64_t m_Fence{0u};
        State m_State{State::Empty};
    };

    using StepRunClock = std::function<std::chrono::steady_clock::time_point()>;
    struct StepRunObservation;

    // The owner's code. Step runs on a worker; Preview on a worker or on the main thread, never beside Step; the rest
    // on the main thread in the owner's context. Commit runs only while no step runs, but Valid, Show and Discard may
    // run while a step does: they must not read what Step changes (Show gets its own copy; Valid checks the captures).
    // Without Step the owner drives its iterations itself (in its own main-thread maintenance, e.g. GPU pages) and
    // tells the run through StepRunsService::Driver.
    struct StepRunCallbacks
    {
        std::function<StepOutcome(StepRunMetric& metric)> Step{}; // one iteration; fills the metric's values
        // Optional, beside Step (ARCH-023): a worker step that may complete several logical steps in one bounded
        // submission (exact farthest-point samples, explicit passes), as an owner-driven batch does. `allowance`
        // (at least 1) is what is left of a step n and of the cap, cut at the live preview cadence; `completed`
        // receives how many completed (1..allowance), the outcome and the metric are the last one's. Every step of a
        // batch counts, also when a pause or stop arrived while it ran (they apply after it).
        std::function<StepOutcome(StepRunMetric& metric, std::uint32_t allowance, std::uint32_t& completed)>
            StepBatch{};
        // Copies the newest state into the preview's Values (Generation and Step are set), or records a device copy
        // and sets its Fence. False: no copy now (no free device slot): nothing is published or counted, and the
        // run goes on. None: no preview.
        std::function<bool(StepPreview& preview)> Preview{};
        std::function<StepRunShow(const StepPreview& preview)> Show{}; // a newer copy to display
        std::function<bool()> Valid{};                             // the captures are current (world and inputs)
        // Publishes once. It may start and answer Pending (an owner-driven run's in its pump, a worker run's in its
        // holder's completion); the owner answers through StepRunDriver::Publishing and CommitFinished, in its
        // context, and also calls CommitFinished when a discard won first (that settles the run).
        std::function<StepRunCommit(std::string& diagnostic)> Commit{};
        // Releases what the run shows, once, as soon as it ends without a commit (even while a step still runs: only
        // main-thread code may touch what it shows). Commit releases it itself.
        std::function<void()> Discard{};
        // Optional, the owner's initializer for run_control restart: called once nothing of the run is in flight, it
        // puts the owner back to its initial state so the next step is step 1 of a new generation (no output of the
        // older computation is kept or shown). False with the reason: nothing changed, the run goes on as it was.
        // Without it the run cannot restart.
        std::function<bool(std::string& diagnostic)> Restart{};
        // Optional, a worker run's (ARCH-023): the owner's Init on the main thread finished (its workspace, write
        // slots and constant uploads are prepared), so a segment may start. Until then the run keeps its controls
        // (running, step n) and computes nothing; a restart's new Init is gated the same way. Without it a segment
        // starts as soon as the run computes.
        std::function<bool()> Prepared{};
        // Optional, the settled end: called once, on the main thread in the owner's context, after the run is
        // terminal, nothing of it is in flight (no step runs) and its holder job finished (the output is free), with
        // the run's final observation. Never from inside Discard (a Settle nested in it leaves it to the outer one),
        // never after Abandon or a refused Start. Reentrancy: every control of the run is refused then; a new run on
        // the same output is admitted when the holder ended by a cancel, but may be refused as busy when the holder
        // ended by publishing (its job is still the job service's completing job until Finished returns).
        std::function<void(const StepRunObservation& run)> Finished{};
    };

    struct StepRunOptions
    {
        std::string Label{};  // its Jobs row
        std::string Owner{};  // a plugin's or module's name
        PluginJobOutput Output{};
        // The cap, live preview (while computing every PreviewEvery steps and PreviewIntervalMs apart; 0: none, no
        // copy is made while computing or resting; a paused or ended run otherwise shows its state whatever its step
        // count), automatic Accept and the start mode (Start: ready, or running at once).
        StepRunExecution Execution{};
        bool DevicePreview{false}; // this live-preview route uses a device ring (not a terminal-only result ring)
        std::array<std::string, 4> MetricNames{};
        StepRunClock Clock{}; // monotonic; steady_clock by default (tests inject one)
    };

    // How the owner submits and cancels its jobs (a plugin: PluginStepRunJobs()). Called on the main thread in the
    // owner's context: from Start and from the run's pump and completions.
    struct StepRunJobs
    {
        std::function<PluginJobHandle(PluginJob job, std::string* refusal)> Submit{};
        std::function<bool(PluginJobHandle job)> Cancel{}; // cancels the job's run (all its jobs)
    };
    // With `holder`, the run's first job (its holder) is stored there once submitted, so an owner-driven plugin run
    // joins its own stages to the run (PluginJob::Run): the Jobs window's cancel and an unload reach them with it.
    [[nodiscard]] StepRunJobs PluginStepRunJobs(std::shared_ptr<PluginJobHandle> holder = {});
    // A runtime module's run as editor jobs of `surface` under `identity`: rows of its Jobs window, a cancel cancelling
    // the whole run (a surface without a job listing cancels the job alone) through the surface's `Retire` when bound,
    // so retiring the run's jobs is never remembered as a cancel request. `holder` receives the first job (the
    // run's token, which later stages such as Accept join).
    [[nodiscard]] StepRunJobs EditorStepRunJobs(
        EditorJobCommandSurface surface, EditorJobIdentity identity, std::shared_ptr<JobToken> holder);

    struct StepRunObservation
    {
        std::uint64_t Id{0u};
        std::string Label{}, Owner{}, Diagnostic{};
        std::array<std::string, 4> MetricNames{};
        StepRunPhase Phase{StepRunPhase::Ready};
        StepRunEnd End{StepRunEnd::None};
        std::uint64_t Generation{0u};
        std::uint32_t Steps{0u};
        bool SegmentInFlight{false}; // a worker segment or the owner's recording
        bool WorkerInFlight{false}; // a worker segment alone (its Step may be running)
        std::uint64_t PreviewCopies{0u}; // every copy made, over all generations
        bool CanRestart{false}; // the owner gave a Restart initializer
        // Failed because a step itself answered Failed (its reason is the owner's), not a refused step or Commit.
        bool StepFailed{false};
        StepRunExecution Execution{};
        // Configured live device-ring capacity; 0 when preview is off or this route has no device ring.
        // Not an allocation count: slots are lazy, and terminal result rings are separate.
        std::uint32_t DevicePreviewBuffers{};
        std::optional<std::uint32_t> ShownStep{}; // the step of the copy shown last (this generation)
        std::shared_ptr<const Core::AppendLog<StepRunMetric>>
            Metrics{}; // this generation's bounded records; Steps counts every logical step
    };

    // A run that ended without a commit, as an owner's result status: "failed" (a step or Commit failed), "stale" (its
    // captures changed), "discarded" (run_control or a run row's Discard) or "cancelled" (a Jobs window cancel, an
    // unload, the owner shutting down). For the owner's end: its Finished (the settled end), or its Discard callable,
    // which runs at once when the run is terminal.
    [[nodiscard]] std::string_view UncommittedStatus(const StepRunObservation& run) noexcept;

    struct StepRun; // host data: controls, observation and the owner's callables until the run retires

    // An owner-driven run's side of its owner (main thread). Every call after the run retired is ignored.
    class StepRunDriver
    {
    public:
        StepRunDriver() = default;
        explicit StepRunDriver(std::weak_ptr<StepRun> run) : m_Run(std::move(run)), m_Bound(true) {}
        // The owner may record its next iteration: computing is asked for, nothing ended, nothing waits.
        [[nodiscard]] bool MayStart() const;
        // ARCH-023: how many iterations the owner may record now, for a submission that batches several (0 unless
        // MayStart): what is left of a step n, and of the cap. Run asks for no fewer than the owner's own batch.
        [[nodiscard]] std::uint32_t Allowance() const;
        // Work of the run is submitted and not yet consumed (true) or none is; a pause settles, Accept and a
        // retirement wait, until it is false.
        void Recording(bool inFlight);
        // `count` iterations completed (1, or a batched submission's, all with `outcome` on the last): counted, one
        // record logged for the last (the driver numbers it), the cap and the step budget applied, convergence
        // winning over a simultaneous cap, an earlier Stop over both. Every iteration of a batch counts, also when a
        // pause or stop arrived while it ran. True: the next iteration may be recorded now.
        bool Stepped(StepRunMetric metric, StepOutcome outcome, std::uint32_t count = 1u);
        // The owner may make its own preview copy now: live preview is on, the run's time limit allows one and, while
        // computing, the step count is on its cadence (PreviewEvery). Nothing is counted: once the copy is actually
        // submitted (a write slot acquired, its commands queued) the owner calls Previewed.
        [[nodiscard]] bool MayPreview() const;
        // The same for a copy of the state the run will have after `step` (the end of a batched submission that
        // records its own copy): live preview on, `step` on the cadence and the time limit passed.
        [[nodiscard]] bool MayPreviewAt(std::uint32_t step) const;
        // The owner submitted a preview copy MayPreview allowed: counted and the time limit restarts.
        void Previewed();
        // The owner may start its Commit now (its own Accept, or a batch's automatic one): the run rests (nothing
        // computing or in flight, no restart waiting) or accepts already. False for an ended or unknown run.
        [[nodiscard]] bool MayCommit() const;
        // The owner started its Commit itself (its own Accept, or a batch's automatic one) from a run that is not
        // computing: the run is Accepting with that Commit pending, so a second Accept is refused and CommitFinished
        // answers it. False (nothing changes) when the run is terminal, computing or in flight; true when already
        // Accepting.
        bool Committing();
        // A pending Commit's publication starts now (a discard or cancel no longer wins): true. False when the run
        // already ended (a discard or cancel won first): nothing may be published. Always true for an empty handle
        // (no run controls this owner).
        bool Publishing();
        void CommitFinished(bool committed, std::string diagnostic = {});
        // The owner goes away (its module shuts down, its state is destroyed): the run ends Discarded unless it ended
        // already, what the owner had in flight counts as consumed, and the owner's callables are dropped now, so
        // nothing of the run retains or calls into the destroyed owner. Call it before the owner's state dies. A
        // worker segment that still runs keeps only its Step, StepBatch and Preview (and the clock) until it returns;
        // the owner makes it give up (seals its pages) and keeps what those capture alive.
        void Abandon();

    private:
        std::weak_ptr<StepRun> m_Run{};
        bool m_Bound{false};
    };

    // Main thread. Runs by id; a retired run (terminal, its owner's callables and previews dropped) stays observable,
    // the newest 32 of them (older ones go at the next call). C++ linkage: the editor processing context borrows it
    // without importing this module.
    extern "C++" class StepRunsService
    {
    public:
        StepRunsService();
        ~StepRunsService();
        StepRunsService(const StepRunsService&) = delete;
        StepRunsService& operator=(const StepRunsService&) = delete;

        // In the owner's context: submits the holder job. 0 with `refusal` when the callables are incomplete or the
        // owner refused the job (another run holds the output, ...). The configured start mode applies; a running
        // zero-cap run ends before a step.
        [[nodiscard]] std::uint64_t Start(StepRunOptions options, StepRunCallbacks callbacks, StepRunJobs jobs,
                                          std::string& refusal);
        // A control (count: the steps of Step); false with the reason when refused. Restart requests the owner's
        // Restart initializer (refused without one). The UI's control row and MCP run_control both call this.
        bool Control(std::uint64_t run, StepRunAction action, std::uint32_t count, std::string& refusal);
        // An owner-driven run's handle (empty for an unknown run).
        [[nodiscard]] StepRunDriver Driver(std::uint64_t run);
        // Latest wins: `init` replaces a pending one (never called then) and is applied once nothing is in flight; the
        // run then begins a new generation from step 0, using the start mode or continuous Run intent captured at
        // the request. A later Pause/Stop leaves it ready; an old completion cannot change that intent. Nothing of
        // the older generation shows. A refused `init` leaves the run unchanged, its reason
        // the diagnostic. False when the run is terminal or accepting.
        bool Restart(std::uint64_t run, std::function<bool(std::string& diagnostic)> init, std::string& refusal);
        [[nodiscard]] std::optional<StepRunObservation> Observe(std::uint64_t run);
        [[nodiscard]] std::vector<std::uint64_t> Runs();

        // MCP: run_control and run_observe.
        void RegisterOperations(AgentOperationRegistry& registry);

    private:
        std::map<std::uint64_t, std::shared_ptr<StepRun>> m_Runs{};
        std::uint64_t m_NextId{1u};
        void Trim();
    };

    // ARCH-023: a one-shot method as a worker step run (the scalar plugins, outlier analysis, normal estimation,
    // keypoint and descriptor analysis, the method template): its one step computes the whole result on a worker, its
    // one Commit publishes it, and a restart computes again from the same capture. Compute and Metric run on a worker;
    // the rest on the main thread in the owner's context, Valid and Abort also while the step runs (they must not read
    // what Compute writes then). Its execution is OneShotExecution of the caller's (kOneShotStepRunExecution).
    struct OneShotWorkerCallbacks
    {
        std::function<bool()> Compute{}; // the one step; false: it failed (its reason is the owner's)
        std::function<void(StepRunMetric& metric)> Metric{}; // optional: after a Compute that succeeded
        std::function<bool()> Valid{}; // the captures are current; none: always
        // Accept (no step runs): publishes once; false with the reason when refused. The owner delivers its own
        // result for both answers; Ended is not called after it.
        std::function<bool(std::string& diagnostic)> Publish{};
        // Once, the run ended without a Publish call (discard, cancel, stale captures, a failed or refused step), at
        // its settled end (StepRunCallbacks::Finished): the step has returned and the holder released the output, so
        // Compute's state may be read or freed and a new run on the output is admitted (see Finished for a start from
        // inside it). `run` says why (UncommittedStatus); run.StepFailed: Compute itself answered false.
        std::function<void(const StepRunObservation& run)> Ended{};
        std::function<void()> Restart{}; // optional: forgets the computed result (no step runs)
        // Optional: at once when the run ends without a commit, also while the step runs: a waiting step gives up
        // (a device page); the result still arrives through Ended.
        std::function<void()> Abort{};
    };
    // Starts the run with `options` (its Execution reduced to OneShotExecution): its id, or 0 with `refusal`.
    [[nodiscard]] std::uint64_t StartOneShotWorkerRun(StepRunsService& runs,
        StepRunOptions options,
        OneShotWorkerCallbacks callbacks,
        StepRunJobs jobs,
        std::string& refusal);

    class StepRunsModule final : public IRuntimeModule
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override { return "Runtime.Services.StepRuns"; }
        [[nodiscard]] RuntimeModuleResult OnRegister(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext&) override {}
        [[nodiscard]] StepRunsService& Service() noexcept { return m_Service; }

    private:
        StepRunsService m_Service{};
    };
}
