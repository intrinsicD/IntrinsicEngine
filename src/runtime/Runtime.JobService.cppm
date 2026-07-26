module;

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

export module Extrinsic.Runtime.JobService;

import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.CommandContext;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    // Execution-lane taxonomy. RUNTIME-194 moved this from
    // `Runtime.StreamingExecutor` to the one surviving execution surface;
    // `StreamingExecutor` re-exports it until that module is retired.
    export namespace RuntimeTaskKinds
    {
        inline constexpr Core::Dag::TaskKind Generic{0u};
        inline constexpr Core::Dag::TaskKind AssetIO{1u};
        inline constexpr Core::Dag::TaskKind AssetDecode{2u};
        inline constexpr Core::Dag::TaskKind AssetUpload{3u};
        inline constexpr Core::Dag::TaskKind GeometryProcess{4u};
        inline constexpr Core::Dag::TaskKind PhysicsStep{5u};
        inline constexpr Core::Dag::TaskKind RenderPass{6u};
    }

    export struct JobTokenTag;
    export using JobToken = Core::StrongHandle<JobTokenTag>;

    export struct GpuQueueParticipantTag;
    export using GpuQueueParticipantHandle =
        Core::StrongHandle<GpuQueueParticipantTag>;

    export using JobResultTypeKey = std::size_t;

    export template <typename TResult>
    [[nodiscard]] consteval std::string_view JobResultTypeNameOf() noexcept
    {
#if defined(__clang__) || defined(__GNUC__)
        return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
        return __FUNCSIG__;
#else
        return "JobResultTypeNameOf<unknown>";
#endif
    }

    export enum class JobTarget : std::uint8_t
    {
        CpuPool,
        GpuQueue,
    };

    export enum class JobState : std::uint8_t
    {
        Invalid,
        // Submitted, but at least one dependency has not finished. Released to
        // `Queued` by a completion drain, so dependency release is deterministic
        // and main-thread-ordered rather than racing on a worker.
        AwaitingDependencies,
        Queued,
        Running,
        AwaitingGate,
        // Finished on a worker, but the caller's readiness gate says the result
        // cannot be applied yet (for example a GPU readback that has not landed).
        // The record stays parked across drains without blocking the queue.
        AwaitingApply,
        Published,
        Dropped,
        Cancelled,
        Rejected,
        // Revalidation on the main thread rejected the result before it was
        // allowed to mutate anything.
        StaleDiscarded,
    };

    export struct JobDependency
    {
        JobToken    Job{};
        std::string Reason{};
    };

    // Fail-closed revalidation run on the main thread immediately before a
    // result is allowed to mutate anything. Anything other than `Current`
    // discards the result without applying it.
    export enum class JobApplyValidation : std::uint8_t
    {
        Current,
        Cancelled,
        StaleWorld,
        StaleGeneration,
        MissingTarget,
    };

    export struct JobProgress
    {
        float Normalized{0.0f};
        bool  Determinate{true};
    };

    export class JobCancellation
    {
    public:
        JobCancellation() = default;

        [[nodiscard]] bool IsCancelled() const noexcept;

    private:
        friend class JobService;
        explicit JobCancellation(std::shared_ptr<std::atomic<bool>> flag)
            : m_Flag(std::move(flag))
        {
        }

        std::shared_ptr<std::atomic<bool>> m_Flag{};
    };

    export class JobResultEnvelope
    {
    public:
        JobResultEnvelope() = default;

        template <typename TResult>
        [[nodiscard]] static JobResultEnvelope Make(TResult payload)
        {
            return JobResultEnvelope(Core::TypeToken<TResult>(),
                                     std::make_shared<const TResult>(
                                         std::move(payload)),
                                     JobResultTypeNameOf<TResult>());
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return static_cast<bool>(m_Payload);
        }
        [[nodiscard]] std::string_view TypeName() const noexcept
        {
            return m_TypeName;
        }

        template <typename TResult>
        [[nodiscard]] const TResult* TryGet() const noexcept
        {
            if (m_Type != Core::TypeToken<TResult>())
                return nullptr;
            return static_cast<const TResult*>(m_Payload.get());
        }

    private:
        friend class JobService;

        JobResultEnvelope(JobResultTypeKey type,
                          std::shared_ptr<const void> payload,
                          std::string_view typeName)
            : m_Type(type), m_Payload(std::move(payload)), m_TypeName(typeName)
        {
        }

        JobResultTypeKey           m_Type{0};
        std::shared_ptr<const void> m_Payload{};
        std::string_view           m_TypeName{};
    };

    export struct JobDesc
    {
        std::string DebugName{};
        JobTarget Target{JobTarget::CpuPool};
        WorldHandle Scope{DefaultWorldHandle};

        // Scheduling metadata. Dependencies must all reach a terminal state
        // before this job is queued; a dependency that cancels or is discarded
        // cancels this job too, so a chain never applies half its work.
        std::vector<JobDependency> DependsOn{};
        Core::Dag::TaskPriority Priority{Core::Dag::TaskPriority::Normal};
        Core::Dag::TaskKind Kind{RuntimeTaskKinds::Generic};
        std::uint32_t EstimatedCost{1u};

        // Document/world epoch captured at submit time. A drain compares it
        // against the service's current generation for the job's world and
        // discards results captured before a world replacement.
        std::uint64_t CancellationGeneration{0u};

        std::move_only_function<JobResultEnvelope(const JobCancellation&)>
            Work{};

        // Optional readiness gate consulted on the main thread. Returning false
        // parks the finished result for a later drain instead of applying it.
        std::move_only_function<bool()> IsReadyToApply{};

        // Optional fail-closed revalidation, consulted immediately before
        // publication. Callers use it to re-check generations they captured in
        // their immutable snapshot.
        std::move_only_function<JobApplyValidation()> ValidateBeforeApply{};

        std::move_only_function<bool(KernelEventBus&, const JobResultEnvelope&)>
            PublishCompletion{};

        // Main-thread reconciliation for a job that terminates *without*
        // publishing: cancelled, discarded as stale, dependency-cancelled, or
        // dropped. Exactly one of `PublishCompletion` and this runs per
        // submitted job, both on the main thread from a completion drain, so a
        // consumer that owns control state (an ingest state machine, a pending
        // request slot) can never be left waiting on a result that will never
        // arrive. Invoked outside the service lock, so it may submit or cancel.
        //
        // This is the superset of the retired
        // `StreamingTaskDesc::FinalizeCancellationOnMainThread`, which fired on
        // explicit cancellation only; staleness discard is a `JobService`
        // terminal state that a cancellation-only hook would leak.
        //
        // Not called when `Submit` rejects the job — that is reported
        // synchronously by an invalid `JobToken`.
        std::move_only_function<void()> FinalizeUnpublishedOnMainThread{};
    };

    export struct GpuQueueParticipantDesc
    {
        std::string DebugName{};
        WorldHandle Scope{DefaultWorldHandle};
        std::function<void(RHI::ICommandContext&)> RecordFrameCommands{};
        std::function<void()> DrainCompletedTransfers{};
        std::function<bool()> HasInFlightWork{};
        std::function<void()> ShutdownAfterDeviceIdle{};
    };

    export template <typename TResult, typename TWork, typename TEventFactory>
    [[nodiscard]] JobDesc MakeCpuJobDesc(std::string debugName,
                                         WorldHandle scope,
                                         TWork work,
                                         TEventFactory makeEvent)
    {
        JobDesc desc{};
        desc.DebugName = std::move(debugName);
        desc.Target = JobTarget::CpuPool;
        desc.Scope = scope.IsValid() ? scope : DefaultWorldHandle;
        desc.Work = [work = std::move(work)](const JobCancellation& cancellation) mutable
            -> JobResultEnvelope
        {
            return JobResultEnvelope::Make<TResult>(work(cancellation));
        };
        desc.PublishCompletion =
            [makeEvent = std::move(makeEvent)](
                KernelEventBus& events,
                const JobResultEnvelope& result) mutable -> bool
        {
            const TResult* payload = result.TryGet<TResult>();
            if (!payload)
                return false;
            events.Publish(makeEvent(*payload));
            return true;
        };
        return desc;
    }

    // Read-only per-job view for queue UIs and for shutdown paths that must
    // enumerate survivors. RUNTIME-194 Slice B5 moved this off
    // `DerivedJobRegistry::SnapshotAll()`; the record is deliberately generic —
    // domain identity (which entity, which output) stays with the consumer that
    // submitted the job, not on the execution service.
    export struct JobSnapshot
    {
        JobToken      Token{};
        std::string   DebugName{};
        JobState      State{JobState::Invalid};
        WorldHandle   Scope{DefaultWorldHandle};
        JobProgress   Progress{};
        // Age since submit, measured when the snapshot is taken. It keeps
        // growing after a job reaches a terminal state, matching the retired
        // registry's semantics.
        std::uint64_t ElapsedMilliseconds{0};
    };

    export struct JobServiceStats
    {
        std::uint64_t SubmittedJobs{0};
        std::uint64_t RejectedJobs{0};
        std::uint64_t InFlightJobs{0};
        std::uint64_t QueuedJobs{0};
        std::uint64_t RunningJobs{0};
        std::uint64_t AwaitingGateJobs{0};
        std::uint64_t CompletedJobs{0};
        std::uint64_t CancelledJobs{0};
        std::uint64_t PublishedCompletions{0};
        std::uint64_t DroppedCompletions{0};
        std::uint64_t CompletionDrains{0};
        std::uint64_t LastDrainFinished{0};
        std::uint64_t LastDrainPublished{0};
        std::uint64_t LastDrainDropped{0};
        std::uint64_t ReapedJobs{0};
        std::uint64_t AwaitingDependencyJobs{0};
        std::uint64_t AwaitingApplyJobs{0};
        std::uint64_t StaleDiscardedJobs{0};
        std::uint64_t DependencyCancelledJobs{0};
        std::uint64_t LastDrainParked{0};
        std::uint64_t LastDrainStaleDiscarded{0};
        std::uint64_t LastDrainBudget{0};
        std::uint64_t FinalizedUnpublishedJobs{0};
        std::uint64_t PendingUnpublishedFinalizers{0};
        std::uint64_t LastDrainFinalizedUnpublished{0};
    };

    export struct JobServiceTestHooks
    {
        // Invoked on the worker after its completion record becomes drainable
        // and the completion-queue mutex has been released. Contract tests use
        // this to force main-thread drain/worker-return interleavings.
        std::function<void(JobToken)> AfterCompletionQueued{};
    };

    export class JobService
    {
    public:
        explicit JobService(JobServiceTestHooks testHooks = {});
        ~JobService();

        JobService(const JobService&) = delete;
        JobService& operator=(const JobService&) = delete;

        [[nodiscard]] JobToken Submit(JobDesc desc);
        [[nodiscard]] bool Cancel(JobToken token);
        [[nodiscard]] std::uint64_t CancelAllForWorld(WorldHandle world);
        [[nodiscard]] std::uint64_t CancelAll();

        [[nodiscard]] bool IsComplete(JobToken token) const;
        [[nodiscard]] JobState GetState(JobToken token) const;

        // Unbounded drain. Prefer the bounded overload on the frame path.
        [[nodiscard]] std::uint64_t DrainCompletions(KernelEventBus& events);

        // Bounded main-thread apply: applies at most `maxApplyCount` results and
        // leaves the rest queued for the next drain, so a completion burst can
        // never stall a frame. `maxApplyCount == 0` means unbounded.
        [[nodiscard]] std::uint64_t DrainCompletions(KernelEventBus& events,
                                                     std::uint64_t maxApplyCount);
        [[nodiscard]] std::uint64_t ReapCompleted();

        // Worker-callable progress reporting; `GetProgress` is main-thread safe.
        void ReportProgress(JobToken token, JobProgress progress);
        [[nodiscard]] JobProgress GetProgress(JobToken token) const;

        // Bumps the epoch for `world`, so results captured before the bump are
        // discarded by revalidation instead of mutating replaced state.
        std::uint64_t AdvanceWorldGeneration(WorldHandle world);
        [[nodiscard]] std::uint64_t WorldGeneration(WorldHandle world) const;

        [[nodiscard]] JobServiceStats Stats() const;

        // Every job the service still retains, ordered by token so a queue view
        // is stable across frames. Reaped jobs are gone; call `ReapCompleted()`
        // deliberately rather than relying on this to prune.
        [[nodiscard]] std::vector<JobSnapshot> SnapshotAll() const;

        [[nodiscard]] GpuQueueParticipantHandle RegisterGpuQueueParticipant(
            GpuQueueParticipantDesc desc);
        void UnregisterGpuQueueParticipant(
            GpuQueueParticipantHandle handle,
            std::function<void()> waitForGpuIdle = {});
        void RecordGpuQueueFrameCommands(RHI::ICommandContext& commandContext);
        [[nodiscard]] std::uint64_t DrainGpuQueueCompletedTransfers();
        [[nodiscard]] bool HasGpuQueueWork() const;
        [[nodiscard]] std::uint64_t ShutdownGpuQueueParticipants(
            std::function<void()> waitForGpuIdle = {});

    private:
        struct SharedState;
        struct JobRecord;

        // Defined in the implementation unit; queues a record whose
        // dependencies are satisfied onto the shared CPU pool.
        static void DispatchJob(const std::shared_ptr<SharedState>& state,
                                const std::shared_ptr<JobRecord>& job);

        // Releases dependency-blocked jobs whose upstream work is terminal, and
        // cancels those whose upstream did not publish.
        void ReleaseSatisfiedDependencies();

        // Claims `job`'s unpublished-finalizer exactly once and queues it for
        // the next main-thread drain. Callable from a worker; the caller must
        // hold the state mutex.
        static void QueueUnpublishedFinalizerLocked(
            SharedState& state,
            const std::shared_ptr<JobRecord>& job);

        // Runs queued unpublished finalizers on the main thread, outside the
        // service lock. Returns how many ran.
        [[nodiscard]] std::uint64_t RunUnpublishedFinalizers();

        [[nodiscard]] JobApplyValidation ResolveApplyValidation(
            JobRecord& job) const;

        std::shared_ptr<SharedState> m_State;
    };
}
