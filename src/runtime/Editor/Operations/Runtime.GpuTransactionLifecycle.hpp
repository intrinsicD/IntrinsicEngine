// The one lifecycle of a two-phase GPU Run/Accept transaction (RUNTIME-311): up to three
// output rings, the Run job (acquire, poll, preview), Accept (front readbacks, then the
// typed publication), Discard, cancellation and exactly-once terminal delivery. Scalar,
// outlier, normal, smoothing and GPU-position transactions embed `GpuTransactionCore` by
// value and keep their typed capture, math, statistics and publication in its hooks.
// Include after the EditorProcessing, EditorCommon, EditorJobProjection, JobService,
// SpatialIndexCache, GpuPropertyResidency, GpuPropertyBinding, StepRunExecution and StepRuns imports and after
// `Runtime.GeometryProcessingOperations.GpuFront.hpp`; compiled in
// `Runtime.GpuTransactionLifecycle.cpp`. The including unit supplies array, atomic, functional,
// memory, optional, span, string and vector. A unit that steps on a worker (Hooks.Step) also includes
// `Services/Runtime.WorkerGpuPages.hpp`.
#pragma once

extern "C++"
{
    namespace Extrinsic::Runtime
    {
        class WorkerGpuPages; // Runtime.WorkerGpuPages.hpp: a worker-stepped transaction's page gate
    }
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    // Frames the residency may refuse a run's slots before the run fails.
    inline constexpr std::uint32_t kGpuTransactionMaxDeferrals = 600u;

    // A worker-stepped transaction's Init (Hooks.Prepare): done, polled again (the residency deferred, an Init page is
    // in flight), or failed with its reason.
    enum class GpuTransactionPrepare : std::uint8_t
    {
        Ready,
        Pending,
        Failed
    };
    // A transaction's steps: one logical step, or its method's own iterations (Execution's cap and cadence).
    enum class GpuTransactionKind : std::uint8_t
    {
        OneShot,
        Iterative
    };

    struct GpuTransactionRing
    {
        Graphics::GpuPropertyKey Key{};
        // The ring generation this run acquired (0: none yet). The run discards only this
        // generation, never a successor's ring on the same key, and is stale once the key's
        // ring is another generation.
        std::uint64_t Generation{};
        std::optional<Graphics::GpuPropertyView> Back{}; // the write slot the run holds
        bool ReadBack{};                                   // Accept reads this ring's front back
        std::shared_ptr<GpuFrontReadback> Readback{};
    };

    // Typed behaviour of one transaction. Hooks capture their owner by raw pointer (the core
    // lives inside it); jobs keep the owner alive through the aliasing core handle.
    struct GpuTransactionHooks
    {
        // The typed inputs are unchanged (the core checks abandonment, the world and the rings).
        std::function<bool()> Current{};
        // Run job readiness on the main thread: acquire slots, queue device work, publish
        // previews. True once the device work ended (a failure included).
        std::function<bool()> Poll{};
        // The Run job's device work ended and is current: fold its statistics, then either end
        // the transaction (`FinishGpuTransaction`) or mark it ready (`ReadyGpuTransaction`).
        std::function<void()> CompleteRun{};
        // Every front readback landed and the transaction is current: the typed publication,
        // which ends the transaction (Applied, or Failed/Discarded with the typed reason).
        std::function<void()> CompleteAccept{};
        // Optional: Accept (a user's or the automatic one) was queued; the typed result may
        // say so while the readback runs.
        std::function<void()> Accepting{};
        // Optional: the step run of an owner that controls this accept-only transaction (resident LOP binds its
        // StepRunDriver::Publishing here): the publication starts now. False: that run ended first (a discard or
        // cancel won), so nothing is published and the transaction ends Discarded, as a transaction's own step run
        // decides at the same boundary.
        std::function<bool()> Publishing{};
        // Optional, a worker-stepped transaction's initializer for run_control
        // restart, called once nothing of the run is in flight: the typed state returns to its captured input (the
        // workspaces are begun again, RestartGpuTransactionRings drops the older fronts). False with the reason:
        // nothing changed.
        std::function<bool(std::string& diagnostic)> Restart{};
        // ARCH-023, a worker-stepped transaction (GpuTransactionCore::Pages): its logical steps run on its step run's
        // worker, each submitting and waiting for its own bounded pages (no frame paces them).
        //   Prepare (main thread, the Run job's readiness until it answers Ready; again after a restart): the Init. It
        //   resolves and leases what the steps read or write (inputs, workspaces, write slots), allocates their
        //   capacity and submits the run's constant uploads (an owner page it waits for), and captures the immutable
        //   views the worker records with. No segment starts before it answered Ready; Failed ends the run.
        //   Step (worker): one logical step through Core.Pages, touching only the typed worker state and what Prepare
        //   captured, never ECS, residency, caches or typed owner fields. A failing step sets Core.WorkerFailure.
        //   StepBatch (worker, optional): several logical steps in one bounded submission (StepRunCallbacks::StepBatch).
        //   Preview (worker, or the owner at a rest) and Show (owner): optional live preview, as StepRunCallbacks'.
        //   Uses (owner): the residency slots the steps read or write besides the rings' write slots (inputs, bases);
        //   before Release (and a ring restart) drops them, the core seals the pages and stamps these and the write
        //   slots with the compute fence, so no slot is reused or freed before the worker's last page completed.
        // Poll is then the owner's side and may run while a step runs (it reads worker state only once
        // GpuTransactionStepCompleted, or while GpuTransactionWorkerBusy is false): true once the result may be made
        // ready (CompleteRun).
        std::function<GpuTransactionPrepare(std::string& diagnostic)> Prepare{};
        std::function<StepOutcome(StepRunMetric& metric)> Step{};
        std::function<StepOutcome(StepRunMetric& metric, std::uint32_t allowance, std::uint32_t& completed)>
            StepBatch{};
        std::function<bool(StepPreview& preview)> Preview{};
        std::function<StepRunShow(const StepPreview& preview)> Show{};
        std::function<void(std::vector<RHI::BufferHandle>& buffers)> Uses{};
        // Drops the typed input views and the workspaces. Recorders capture their own workspace
        // leases (the spatial cache keeps a recorder until its readback is safe), so the run
        // returns its copies at once, also when it ends while device work is in flight. A worker-stepped
        // transaction's Release also drops what its steps captured (the worker's workspace and index references):
        // the core calls it once no step can run any more (at once when none runs at the end, else at the step run's
        // settled end), so a finished transaction a caller keeps pins no lease.
        std::function<void()> Release{};
        // The one terminal delivery: the typed result takes `status`/`message` and goes to the
        // sink. Called at most once per transaction; the result is frozen afterwards.
        // A discard reports immediately from the owner pump, before an in-flight worker necessarily returns.
        // Finished protects the later typed Release; a new run of this output can remain busy until then.
        std::function<void(EditorCommandStatus, std::string)> Deliver{};
    };

    struct GpuTransactionCore
    {
        EditorProcessingContext Context{};
        // The output identity. Every job of the run carries it; once the Run job is queued,
        // `Identity.Run` names the run so the Accept stage joins it.
        EditorJobIdentity Identity{};
        Graphics::GpuPropertyResidency* Residency{};
        std::array<GpuTransactionRing, 3> Rings{};
        std::size_t RingCount{};
        // The device work of the Run job (null until queued).
        std::shared_ptr<SpatialGpuResult> Gpu{};
        EditorGpuTransactionPhase Phase{EditorGpuTransactionPhase::Running};
        std::uint32_t Deferrals{};
        bool Recorded{}; // this generation's device work was asked for (the step run allowed it)
        bool RunJobLive{}; // the Run job is queued and has not ended (a restart after its end queues another)
        bool Abandoned{};   // discarded or cancelled: nothing of it may publish any more
        bool Delivered{};   // the terminal result went out (or a rejected start closed it)
        bool Publishing{};  // Accept's typed publication runs (history observers may Discard)
        bool TestFront{};   // test seam: the fronts are supplied, Accept reads nothing back
        // RunToken: the run's first Run job (the run's identity; a restarted Run job joins it).
        JobToken RunToken{}, AcceptToken{};
        // ARCH-023: the step run (of `Context.StepRuns`) that controls a transaction with a Run job, with `Execution`:
        // a one-shot's one logical step (restartable with its typed initializer), or an
        // worker-stepped transaction's own steps and restart; then run_control/run_observe Accept or Discard as the
        // panel's do. A transaction with a Run job is refused
        // without the service. 0: an Accept-only transaction, whose owner drives its own step run (K-Means, LOP).
        std::uint64_t StepRun{};
        // ARCH-023: the step run's execution options, the one place they are decided. A one-shot (Kind) uses only
        // their Start (ready: nothing recorded before run or step n) and AutoAccept (a batch: End, it accepts once
        // ready unless stopped), normalized to one step without live preview (OneShotExecution); an iterative or
        // worker-stepped transaction passes its method's whole options. The default is an interactive one-shot's.
        StepRunExecution Execution{kOneShotStepRunExecution};
        // Commit during worker execution requests publication once its latest complete result is resident.
        bool CommitWanted{};
        // ARCH-023: a worker-stepped transaction's page gate (Hooks.Step; null otherwise), whether its Init answered
        // Ready for this generation, and why its failing step failed (written by the step before it returns Failed,
        // read on the main thread once the segment ended). Its Run job lasts until Poll answers true; a Commit while
        // it runs sets CommitWanted.
        std::shared_ptr<WorkerGpuPages> Pages{};
        // One logical step (scalar, normals, outliers, a producer's traversal) or the method's own steps (smoothing,
        // sampling). Not an option: the method's algorithm; its options are Execution's.
        GpuTransactionKind Kind{GpuTransactionKind::OneShot};
        bool Prepared{};
        // ARCH-023, a worker-stepped run: a step completed without failing (set on the worker after its last write; a
        // restart clears it), and Hooks.Release is due but waits while a segment is in flight (for the step run's
        // settled end, StepRunCallbacks::Finished).
        std::atomic<bool> StepCompleted{false};
        bool ReleaseDue{};
        std::string WorkerFailure{};
        // Names the transaction in shared wording, e.g. "Vulkan normals".
        std::string Label{};
        // The operation's queued-job label, as its CPU run names it: the duplicate refusal and
        // the rejected-submission wording (`MeshSupport::QueuedJobRejectedMessage`) use it.
        std::string JobLabel{};
        std::string Owner{"Runtime.GpuTransaction"}; // registered plugin identity, separate from the job label
        // The Accept job's debug name, the same for a user and an automatic Accept (jobs lists
        // and tests that intercept a submission name it).
        std::string AcceptJobName{};
        std::string RunJobName{}; // the Run job's debug name (a restart's Run job uses it too)
        GpuTransactionHooks Hooks{};
    };
    using GpuTransactionHandle = std::shared_ptr<GpuTransactionCore>;

    // The aliasing handle of a typed transaction's embedded core (shares its ownership).
    template <class Typed>
    [[nodiscard]] GpuTransactionHandle GpuTransactionOf(const std::shared_ptr<Typed>& typed)
    {
        return typed ? GpuTransactionHandle(typed, &typed->Core) : GpuTransactionHandle{};
    }

    // Not abandoned, its editor session still attached (Context.AttachmentActive), the world current, every acquired
    // ring still this run's generation and the typed inputs current.
    [[nodiscard]] bool GpuTransactionCurrent(const GpuTransactionCore&);
    [[nodiscard]] bool GpuTransactionTerminal(const GpuTransactionCore&) noexcept;

    // A write slot of ring `index` for `ref`. Never acquires in a ring another run created:
    // `Foreign` when the key has a ring this run did not acquire.
    enum class GpuRingAcquisition : std::uint8_t { Ready, Deferred, Foreign };
    [[nodiscard]] GpuRingAcquisition AcquireGpuTransactionBack(
        GpuTransactionCore&, std::size_t index, entt::entity, const GeometryPropertyRef& ref,
        std::uint32_t count, std::uint32_t depth);
    // Counts one refused frame; true once the budget is spent (the caller then fails the run).
    [[nodiscard]] bool GpuTransactionDeferralsExhausted(GpuTransactionCore&) noexcept;

    // Releases what the run holds (typed views and workspaces through `Release`, back slots,
    // readbacks, its own ring generations) and ends it in `phase`, delivering once. The run's
    // job outcome (`EditorJobCommandSurface::ReportOutcome`) is GpuCompute once the device produced
    // a result (Applied, or ended after it was ready), with `message` as its diagnostic.
    void FinishGpuTransaction(GpuTransactionCore&, EditorGpuTransactionPhase, EditorCommandStatus, std::string);
    void FailGpuTransaction(GpuTransactionCore&, std::string);
    // The Run's result waits for Accept or Discard (reported as the run's outcome until it ends).
    void ReadyGpuTransaction(GpuTransactionCore&);

    // Refusal of a start before anything is acquired: a duplicate of an active job on the
    // output (Pending, nothing registered), no step-run service (its run controls), no residency,
    // or a ring of the output that awaits Accept or Discard (InvalidProcessingParameters).
    struct GpuTransactionRefusal
    {
        EditorCommandStatus Status{};
        std::string Message{};
        bool OutputBusy{}; // another run owns the output (its job or its unaccepted ring)
    };
    [[nodiscard]] std::optional<GpuTransactionRefusal> GpuTransactionStartRefusal(const GpuTransactionCore&);
    // Queues the Run job and its step run of `Context.StepRuns` (a run-control stage of the same run that names no
    // output; without the service nothing is queued). Its readiness records nothing until the step run computes (a
    // ready run waits for run or step n), then polls `Hooks.Poll`; publication runs `CompleteRun` and,
    // for an automatic run, Accept (a refused automatic Accept ends the transaction: Discarded
    // when stale, otherwise Failed; a stopped or restarting step run keeps the result waiting instead). A cancel or
    // stale drain finalizes once. A rejected
    // submission closes the transaction without delivery (the caller reports it) and returns
    // an invalid token; the answer is `MeshSupport::QueuedJobRejectedMessage(JobLabel)`.
    [[nodiscard]] JobToken SubmitGpuTransactionRun(const GpuTransactionHandle&, std::string debugName);

    // Marks an owner-submitted terminal store as in flight until its completion is consumed.
    void GpuTransactionRecording(const GpuTransactionCore&, bool inFlight);
    // A restart's fresh output: drops every ring this transaction holds (its fronts, backs and readbacks; buffers
    // are freed after their completions), so the next acquisition creates a new ring generation.
    void RestartGpuTransactionRings(GpuTransactionCore&);
    // ARCH-023: a worker step of the transaction's step run may be running (main thread).
    [[nodiscard]] bool GpuTransactionWorkerBusy(const GpuTransactionCore&);
    // ARCH-023, a one-shot's owner side (main thread): its one worker step completed without failing, so what the
    // step wrote may be read now, in the drain its step run comes to rest, not one later (the segment's completion
    // may still be queued behind the Run job's readiness). A failed step ends through the step run instead.
    [[nodiscard]] bool GpuTransactionStepCompleted(const GpuTransactionCore&);
    // ARCH-023, owner: marks `buffers` (residency slots a worker page read or wrote) with the compute fence, so none
    // is reused or freed before every page submitted so far completed. Ignored without a residency.
    void StampGpuTransactionUses(const GpuTransactionCore&, std::span<const RHI::BufferHandle> buffers);

    // Why Accept cannot start now; empty when it can. An Accept under way answers a caller
    // without a sink Pending (its result goes to the sink already registered); a caller with a
    // sink is refused (InvalidProcessingParameters), since it would never be called. Nothing
    // waiting, or a step run that is computing or restarting, answers InvalidProcessingParameters, changed inputs
    // StaleEntity.
    [[nodiscard]] std::optional<GpuTransactionRefusal> GpuTransactionAcceptRefusal(const GpuTransactionCore&, bool withSink = false);
    // Starts Accept on a transaction `GpuTransactionAcceptRefusal` admitted: one front
    // readback per `ReadBack` ring (none for a test front), then the Accept job that joins the
    // run, publishes through `CompleteAccept` once every readback landed and finalizes once
    // when cancelled or stale. False when it already ended (the front is gone, or the job lane refused the submission).
    bool BeginGpuTransactionAccept(const GpuTransactionHandle&);
    // Ends a live transaction as Discarded with the typed status and reason. Ignored once
    // terminal and while Accept's publication runs.
    void DiscardGpuTransaction(GpuTransactionCore&, EditorCommandStatus, std::string);
}
}
