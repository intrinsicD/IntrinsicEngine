// The one lifecycle of a two-phase GPU Run/Accept transaction (RUNTIME-311): up to three
// output rings, the Run job (acquire, poll, preview), Accept (front readbacks, then the
// typed publication), Discard, cancellation and exactly-once terminal delivery. Scalar,
// outlier, normal, smoothing and GPU-position transactions embed `GpuTransactionCore` by
// value and keep their typed capture, math, statistics and publication in its hooks.
// Include after the EditorProcessing, EditorCommon, EditorJobProjection, JobService,
// SpatialIndexCache, GpuPropertyResidency and GpuPropertyBinding imports and after
// `Runtime.GeometryProcessingOperations.GpuFront.hpp`; compiled in
// `Runtime.GpuTransactionLifecycle.cpp`. The including unit supplies array, functional,
// memory, optional and string.
#pragma once

extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    // Frames the residency may refuse a run's slots before the run fails.
    inline constexpr std::uint32_t kGpuTransactionMaxDeferrals = 600u;

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
        // Drops the typed input views and, when `GpuTransactionWorkReleasable`, the workspaces.
        std::function<void()> Release{};
        // The one terminal delivery: the typed result takes `status`/`message` and goes to the
        // sink. Called at most once per transaction; the result is frozen afterwards.
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
        // The device work of the Run job (null until queued). A workspace may be released
        // only while this is null or Ready: otherwise a recorder may still use it.
        std::shared_ptr<SpatialGpuResult> Gpu{};
        EditorGpuTransactionPhase Phase{EditorGpuTransactionPhase::Running};
        std::uint32_t Deferrals{};
        bool AutoAccept{};  // a batch/agent run accepts as soon as it is ready
        bool Abandoned{};   // discarded or cancelled: nothing of it may publish any more
        bool Delivered{};   // the terminal result went out (or a rejected start closed it)
        bool Publishing{};  // Accept's typed publication runs (history observers may Discard)
        bool TestFront{};   // test seam: the fronts are supplied, Accept reads nothing back
        JobToken RunToken{}, AcceptToken{};
        // Names the transaction in shared wording, e.g. "Vulkan normals".
        std::string Label{};
        // The Accept job's debug name, the same for a user and an automatic Accept (jobs lists
        // and tests that intercept a submission name it).
        std::string AcceptJobName{};
        GpuTransactionHooks Hooks{};
    };
    using GpuTransactionHandle = std::shared_ptr<GpuTransactionCore>;

    // The aliasing handle of a typed transaction's embedded core (shares its ownership).
    template <class Typed>
    [[nodiscard]] GpuTransactionHandle GpuTransactionOf(const std::shared_ptr<Typed>& typed)
    {
        return typed ? GpuTransactionHandle(typed, &typed->Core) : GpuTransactionHandle{};
    }

    // Not abandoned, the world current, every acquired ring still this run's generation and
    // the typed inputs current.
    [[nodiscard]] bool GpuTransactionCurrent(const GpuTransactionCore&);
    [[nodiscard]] bool GpuTransactionWorkReleasable(const GpuTransactionCore&) noexcept;
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
    // readbacks, its own ring generations) and ends it in `phase`, delivering once.
    void FinishGpuTransaction(GpuTransactionCore&, EditorGpuTransactionPhase, EditorCommandStatus, std::string);
    void FailGpuTransaction(GpuTransactionCore&, std::string);
    // The Run's result waits for Accept or Discard.
    void ReadyGpuTransaction(GpuTransactionCore&) noexcept;

    // Refusal of a start before anything is acquired: a duplicate of an active job on the
    // output (Pending, nothing registered), no residency, or a ring of the output that awaits
    // Accept or Discard (InvalidProcessingParameters).
    struct GpuTransactionRefusal
    {
        EditorCommandStatus Status{};
        std::string Message{};
    };
    [[nodiscard]] std::optional<GpuTransactionRefusal> GpuTransactionStartRefusal(
        const GpuTransactionCore&, std::string_view jobLabel);
    // Queues the Run job. Its readiness polls `Hooks.Poll`; publication runs `CompleteRun` and,
    // for an automatic run, Accept (a refused automatic Accept ends the transaction: Discarded
    // when stale, otherwise Failed). A cancel or stale drain finalizes once. A rejected
    // submission closes the transaction without delivery (the caller reports it) and returns
    // an invalid token.
    [[nodiscard]] JobToken SubmitGpuTransactionRun(const GpuTransactionHandle&, std::string debugName);

    // Why Accept cannot start now; empty when it can. An Accept under way answers a caller
    // without a sink Pending (its result goes to the sink already registered); a caller with a
    // sink is refused (InvalidProcessingParameters), since it would never be called. Nothing
    // waiting answers InvalidProcessingParameters, changed inputs StaleEntity.
    [[nodiscard]] std::optional<GpuTransactionRefusal> GpuTransactionAcceptRefusal(const GpuTransactionCore&, bool withSink = false);
    // Starts Accept on a transaction `GpuTransactionAcceptRefusal` admitted: one front
    // readback per `ReadBack` ring (none for a test front), then the Accept job that joins the
    // run, publishes through `CompleteAccept` once every readback landed and finalizes once
    // when cancelled or stale. False when it already ended (front gone, submission rejected).
    bool BeginGpuTransactionAccept(const GpuTransactionHandle&);
    // Ends a live transaction as Discarded with the typed status and reason. Ignored once
    // terminal and while Accept's publication runs.
    void DiscardGpuTransaction(GpuTransactionCore&, EditorCommandStatus, std::string);
}
}
