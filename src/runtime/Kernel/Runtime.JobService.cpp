module;

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

module Extrinsic.Runtime.JobService;

import Extrinsic.Core.Logging;
import Extrinsic.Core.StrongHandle;
import Extrinsic.Core.Tasks;
import Extrinsic.RHI.CommandContext;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] bool IsTerminal(const JobState state) noexcept
        {
            switch (state)
            {
            case JobState::Published:
            case JobState::Dropped:
            case JobState::Cancelled:
            case JobState::Rejected:
            case JobState::StaleDiscarded:
                return true;
            case JobState::Invalid:
            case JobState::AwaitingDependencies:
            case JobState::Queued:
            case JobState::Running:
            case JobState::AwaitingGate:
            case JobState::AwaitingApply:
                return false;
            }
            return false;
        }
    }

    struct JobService::JobRecord
    {
        JobToken Token{};
        WorldHandle Scope{DefaultWorldHandle};
        JobTarget Target{JobTarget::CpuPool};
        std::string DebugName{};
        std::move_only_function<JobResultEnvelope(const JobCancellation&)>
            Work{};
        std::move_only_function<bool(KernelEventBus&,
                                     const JobResultEnvelope&)>
            PublishCompletion{};
        std::shared_ptr<std::atomic<bool>> CancelRequested{
            std::make_shared<std::atomic<bool>>(false)};
        std::atomic<JobState> State{JobState::Queued};

        // RUNTIME-194 scheduling/lifecycle additions.
        std::vector<JobToken> DependsOn{};
        Core::Dag::TaskPriority Priority{Core::Dag::TaskPriority::Normal};
        Core::Dag::TaskKind Kind{RuntimeTaskKinds::Generic};
        std::uint32_t EstimatedCost{1u};
        std::uint64_t CancellationGeneration{0u};
        std::move_only_function<bool()> IsReadyToApply{};
        std::move_only_function<JobApplyValidation()> ValidateBeforeApply{};
        std::atomic<float> ProgressNormalized{0.0f};
        std::atomic<bool> ProgressDeterminate{true};
        std::chrono::steady_clock::time_point SubmittedAt{};
        std::move_only_function<void()> FinalizeUnpublishedOnMainThread{};
        // Set when the finalizer has been queued, so the several terminal-
        // unpublished paths (worker cancel, drain cancel, stale discard,
        // dependency cancel, drop) can each try to claim it and exactly one
        // wins.
        std::atomic<bool> FinalizerClaimed{false};
        // Jobs with an unpublished finalizer are not complete/reapable until
        // that main-thread reconciliation has actually run. This closes the
        // worker-terminal/drain interleaving where a caller could observe a
        // terminal state immediately after an empty drain while the worker had
        // not queued the finalizer yet.
        std::atomic<bool> UnpublishedFinalizerSettled{true};
    };

    struct JobService::SharedState
    {

        struct CompletionRecord
        {
            std::shared_ptr<JobRecord> Job{};
            JobResultEnvelope Result{};
        };

        struct GpuQueueParticipantRecord
        {
            GpuQueueParticipantHandle Handle{};
            std::string DebugName{};
            WorldHandle Scope{DefaultWorldHandle};
            std::function<void(RHI::ICommandContext&)> RecordFrameCommands{};
            std::function<void()> DrainCompletedTransfers{};
            std::function<bool()> HasInFlightWork{};
            std::function<void()> ShutdownAfterDeviceIdle{};
        };

        mutable std::mutex Mutex{};
        std::unordered_map<JobToken,
                           std::shared_ptr<JobRecord>,
                           Core::StrongHandleHash<JobTokenTag>>
            Jobs{};
        std::vector<GpuQueueParticipantRecord> GpuQueueParticipants{};
        std::vector<CompletionRecord> CompletionQueue{};
        std::uint32_t NextTokenIndex{0u};
        std::uint32_t NextGpuQueueParticipantIndex{0u};
        // Jobs whose dependencies have not all reached a terminal state.
        std::vector<std::shared_ptr<JobRecord>> PendingDependencies{};
        // Jobs that terminated without publishing and owe their consumer one
        // main-thread finalizer call.
        std::vector<std::shared_ptr<JobRecord>> PendingFinalizers{};
        std::unordered_map<WorldHandle,
                           std::uint64_t,
                           Core::StrongHandleHash<WorldHandleTag>>
            WorldGenerations{};
        JobServiceStats Stats{};
        JobServiceTestHooks TestHooks{};
    };

    std::string_view ToString(const JobState value) noexcept
    {
        switch (value)
        {
        case JobState::Invalid:
            return "invalid";
        case JobState::AwaitingDependencies:
            return "awaiting-dependencies";
        case JobState::Queued:
            return "queued";
        case JobState::Running:
            return "running";
        case JobState::AwaitingGate:
            return "awaiting-gate";
        case JobState::AwaitingApply:
            return "awaiting-apply";
        case JobState::Published:
            return "published";
        case JobState::Dropped:
            return "dropped";
        case JobState::Cancelled:
            return "cancelled";
        case JobState::Rejected:
            return "rejected";
        case JobState::StaleDiscarded:
            return "stale-discarded";
        }
        return "invalid";
    }

    JobService::JobService(JobServiceTestHooks testHooks)
        : m_State(std::make_shared<SharedState>())
    {
        m_State->TestHooks = std::move(testHooks);
    }

    JobService::~JobService() = default;

    bool JobCancellation::IsCancelled() const noexcept
    {
        return m_Flag && m_Flag->load(std::memory_order_acquire);
    }

    void JobService::QueueUnpublishedFinalizerLocked(
        SharedState& state,
        const std::shared_ptr<JobRecord>& job)
    {
        if (!job || !job->FinalizeUnpublishedOnMainThread)
            return;
        if (job->FinalizerClaimed.exchange(true, std::memory_order_acq_rel))
            return;
        state.PendingFinalizers.push_back(job);
        state.Stats.PendingUnpublishedFinalizers =
            static_cast<std::uint64_t>(state.PendingFinalizers.size());
    }

    std::uint64_t JobService::RunUnpublishedFinalizers()
    {
        if (!m_State)
            return 0;

        std::vector<std::shared_ptr<JobRecord>> pending;
        {
            std::lock_guard lock(m_State->Mutex);
            pending.swap(m_State->PendingFinalizers);
            m_State->Stats.PendingUnpublishedFinalizers = 0;
        }

        std::uint64_t finalized = 0;
        for (const auto& job : pending)
        {
            if (!job)
                continue;
            if (job->FinalizeUnpublishedOnMainThread)
            {
                // Outside the lock: a finalizer may submit or cancel work.
                // Released afterwards so the consumer's captured state does
                // not outlive the one call it is entitled to.
                job->FinalizeUnpublishedOnMainThread();
                job->FinalizeUnpublishedOnMainThread = {};
                finalized += 1;
            }
            job->UnpublishedFinalizerSettled.store(
                true, std::memory_order_release);
        }

        if (finalized != 0)
        {
            std::lock_guard lock(m_State->Mutex);
            m_State->Stats.FinalizedUnpublishedJobs += finalized;
        }
        return finalized;
    }

    JobToken JobService::Submit(JobDesc desc)
    {
        if (!m_State)
            return {};

        if (desc.Target != JobTarget::CpuPool)
        {
            std::lock_guard lock(m_State->Mutex);
            m_State->Stats.RejectedJobs += 1;
            Core::Log::Error(
                "[JobService] Rejected job '{}': JobDesc targets only the CPU "
                "pool; GPU frame work must register through "
                "RegisterGpuQueueParticipant.",
                desc.DebugName);
            return {};
        }

        if (!desc.Work || !desc.PublishCompletion)
        {
            std::lock_guard lock(m_State->Mutex);
            m_State->Stats.RejectedJobs += 1;
            Core::Log::Error(
                "[JobService] Rejected job '{}': work and completion publisher "
                "must both be valid.",
                desc.DebugName);
            return {};
        }

        if (!Core::Tasks::Scheduler::IsInitialized())
        {
            std::lock_guard lock(m_State->Mutex);
            m_State->Stats.RejectedJobs += 1;
            Core::Log::Error(
                "[JobService] Rejected job '{}': shared Core::Tasks scheduler "
                "is not initialized.",
                desc.DebugName);
            return {};
        }

        auto job = std::make_shared<JobService::JobRecord>();
        {
            std::lock_guard lock(m_State->Mutex);
            job->Token = JobToken{m_State->NextTokenIndex++, 1u};
            job->Scope = desc.Scope.IsValid() ? desc.Scope : DefaultWorldHandle;
            job->Target = desc.Target;
            job->DebugName = std::move(desc.DebugName);
            job->SubmittedAt = std::chrono::steady_clock::now();
            job->Work = std::move(desc.Work);
            job->PublishCompletion = std::move(desc.PublishCompletion);
            job->Priority = desc.Priority;
            job->Kind = desc.Kind;
            job->EstimatedCost = desc.EstimatedCost;
            job->CancellationGeneration = desc.CancellationGeneration;
            job->IsReadyToApply = std::move(desc.IsReadyToApply);
            job->ValidateBeforeApply = std::move(desc.ValidateBeforeApply);
            job->FinalizeUnpublishedOnMainThread =
                std::move(desc.FinalizeUnpublishedOnMainThread);
            job->UnpublishedFinalizerSettled.store(
                !static_cast<bool>(job->FinalizeUnpublishedOnMainThread),
                std::memory_order_relaxed);
            job->DependsOn.reserve(desc.DependsOn.size());
            for (const JobDependency& dependency : desc.DependsOn)
            {
                if (dependency.Job.IsValid())
                    job->DependsOn.push_back(dependency.Job);
            }
            m_State->Jobs.emplace(job->Token, job);
            m_State->Stats.SubmittedJobs += 1;

            if (!job->DependsOn.empty())
            {
                // Held until a drain observes every dependency terminal. Doing
                // the release on the main thread keeps chain ordering
                // deterministic instead of racing between workers.
                job->State.store(JobState::AwaitingDependencies,
                                 std::memory_order_release);
                m_State->PendingDependencies.push_back(job);
                m_State->Stats.AwaitingDependencyJobs += 1;
                return job->Token;
            }
        }

        DispatchJob(m_State, job);
        return job->Token;
    }

    void JobService::DispatchJob(
        const std::shared_ptr<SharedState>& state,
        const std::shared_ptr<JobService::JobRecord>& job)
    {
        job->State.store(JobState::Queued, std::memory_order_release);
        Core::Tasks::Scheduler::Dispatch([state, job]() mutable
        {
            const auto finishUnpublished =
                [&state, &job](const JobState terminalState,
                               const bool countDropped)
                {
                    job->State.store(terminalState,
                                     std::memory_order_release);
                    if (state->TestHooks.BeforeWorkerUnpublishedQueued)
                    {
                        state->TestHooks.BeforeWorkerUnpublishedQueued(
                            job->Token);
                    }
                    std::lock_guard lock(state->Mutex);
                    if (countDropped)
                        state->Stats.DroppedCompletions += 1;
                    QueueUnpublishedFinalizerLocked(*state, job);
                };

            if (job->CancelRequested->load(std::memory_order_acquire))
            {
                finishUnpublished(JobState::Cancelled, false);
                return;
            }

            job->State.store(JobState::Running, std::memory_order_release);

            JobResultEnvelope result =
                job->Work(JobCancellation(job->CancelRequested));

            if (job->CancelRequested->load(std::memory_order_acquire))
            {
                finishUnpublished(JobState::Cancelled, false);
                return;
            }

            if (!result.IsValid())
            {
                finishUnpublished(JobState::Dropped, true);
                Core::Log::Error(
                    "[JobService] Job '{}' produced an empty result envelope; "
                    "completion dropped.",
                    job->DebugName);
                return;
            }

            {
                std::lock_guard lock(state->Mutex);
                // Queue insertion is the publication point. AwaitingGate must
                // already be visible before the drain can take this record;
                // after insertion, only the drain may advance its state.
                job->State.store(JobState::AwaitingGate, std::memory_order_release);
                state->CompletionQueue.push_back(SharedState::CompletionRecord{
                    .Job = job,
                    .Result = std::move(result),
                });
            }

            if (state->TestHooks.AfterCompletionQueued)
                state->TestHooks.AfterCompletionQueued(job->Token);
        });
    }

    bool JobService::Cancel(const JobToken token)
    {
        if (!token.IsValid() || !m_State)
            return false;

        std::shared_ptr<JobService::JobRecord> job;
        {
            std::lock_guard lock(m_State->Mutex);
            const auto it = m_State->Jobs.find(token);
            if (it == m_State->Jobs.end())
                return false;
            job = it->second;
            if (!job || IsTerminal(job->State.load(std::memory_order_acquire)))
                return false;

            const bool alreadyCancelled =
                job->CancelRequested->exchange(true, std::memory_order_acq_rel);
            if (!alreadyCancelled)
                m_State->Stats.CancelledJobs += 1;
            return !alreadyCancelled;
        }
    }

    std::uint64_t JobService::CancelAllForWorld(const WorldHandle world)
    {
        if (!world.IsValid() || !m_State)
            return 0;

        std::vector<JobToken> tokens;
        {
            std::lock_guard lock(m_State->Mutex);
            for (const auto& [token, job] : m_State->Jobs)
            {
                if (job && job->Scope == world &&
                    !IsTerminal(job->State.load(std::memory_order_acquire)))
                    tokens.push_back(token);
            }
        }

        std::uint64_t cancelled = 0;
        for (const JobToken token : tokens)
        {
            if (Cancel(token))
                cancelled += 1;
        }
        return cancelled;
    }

    std::uint64_t JobService::CancelAll()
    {
        if (!m_State)
            return 0;

        std::vector<JobToken> tokens;
        {
            std::lock_guard lock(m_State->Mutex);
            tokens.reserve(m_State->Jobs.size());
            for (const auto& [token, job] : m_State->Jobs)
            {
                if (job &&
                    !IsTerminal(job->State.load(std::memory_order_acquire)))
                    tokens.push_back(token);
            }
        }

        std::uint64_t cancelled = 0;
        for (const JobToken token : tokens)
        {
            if (Cancel(token))
                cancelled += 1;
        }
        return cancelled;
    }

    bool JobService::IsComplete(const JobToken token) const
    {
        if (!token.IsValid() || !m_State)
            return false;

        std::lock_guard lock(m_State->Mutex);
        const auto it = m_State->Jobs.find(token);
        if (it == m_State->Jobs.end() || !it->second)
            return false;
        const std::shared_ptr<JobRecord>& job = it->second;
        return IsTerminal(job->State.load(std::memory_order_acquire)) &&
               job->UnpublishedFinalizerSettled.load(
                   std::memory_order_acquire);
    }

    JobState JobService::GetState(const JobToken token) const
    {
        if (!token.IsValid() || !m_State)
            return JobState::Invalid;

        std::lock_guard lock(m_State->Mutex);
        const auto it = m_State->Jobs.find(token);
        if (it == m_State->Jobs.end() || !it->second)
            return JobState::Invalid;
        return it->second->State.load(std::memory_order_acquire);
    }

    std::uint64_t JobService::DrainCompletions(KernelEventBus& events)
    {
        return DrainCompletions(events, 0u);
    }

    std::uint64_t JobService::DrainCompletions(KernelEventBus& events,
                                               const std::uint64_t maxApplyCount)
    {
        if (!m_State)
            return 0;

        std::vector<SharedState::CompletionRecord> batch;
        std::vector<SharedState::CompletionRecord> deferred;
        {
            std::lock_guard lock(m_State->Mutex);
            if (maxApplyCount == 0u ||
                m_State->CompletionQueue.size() <= maxApplyCount)
            {
                batch.swap(m_State->CompletionQueue);
            }
            else
            {
                // Bounded apply: take the budget off the front and leave the
                // remainder queued in order, so a completion burst cannot stall
                // a frame and no record is starved.
                const auto budget = static_cast<std::size_t>(maxApplyCount);
                batch.assign(
                    std::make_move_iterator(m_State->CompletionQueue.begin()),
                    std::make_move_iterator(m_State->CompletionQueue.begin() +
                                            static_cast<std::ptrdiff_t>(budget)));
                m_State->CompletionQueue.erase(
                    m_State->CompletionQueue.begin(),
                    m_State->CompletionQueue.begin() +
                        static_cast<std::ptrdiff_t>(budget));
            }
            m_State->Stats.CompletionDrains += 1;
            m_State->Stats.LastDrainFinished =
                static_cast<std::uint64_t>(batch.size());
            m_State->Stats.LastDrainPublished = 0;
            m_State->Stats.LastDrainDropped = 0;
            m_State->Stats.LastDrainParked = 0;
            m_State->Stats.LastDrainStaleDiscarded = 0;
            m_State->Stats.LastDrainBudget = maxApplyCount;
        }

        std::uint64_t published = 0;
        std::uint64_t dropped = 0;
        std::uint64_t parked = 0;
        std::uint64_t staleDiscarded = 0;
        // Terminated without publishing in this batch; each owes its consumer
        // one finalizer call. Collected here and queued under the epilogue lock
        // rather than locking per record.
        std::vector<std::shared_ptr<JobRecord>> unpublished;
        for (SharedState::CompletionRecord& completion : batch)
        {
            const std::shared_ptr<JobRecord>& job = completion.Job;
            if (!job)
            {
                dropped += 1;
                continue;
            }

            if (job->CancelRequested->load(std::memory_order_acquire))
            {
                job->State.store(JobState::Cancelled, std::memory_order_release);
                unpublished.push_back(job);
                dropped += 1;
                continue;
            }

            // Readiness gate: park rather than apply, and rather than block.
            if (job->IsReadyToApply && !job->IsReadyToApply())
            {
                job->State.store(JobState::AwaitingApply,
                                 std::memory_order_release);
                deferred.push_back(std::move(completion));
                parked += 1;
                continue;
            }

            // Fail-closed revalidation before anything is allowed to mutate.
            const JobApplyValidation validation =
                ResolveApplyValidation(*job);
            if (validation != JobApplyValidation::Current)
            {
                job->State.store(validation == JobApplyValidation::Cancelled
                                     ? JobState::Cancelled
                                     : JobState::StaleDiscarded,
                                 std::memory_order_release);
                unpublished.push_back(job);
                staleDiscarded += 1;
                continue;
            }

            const bool didPublish =
                job->PublishCompletion(events, completion.Result);
            if (didPublish)
            {
                job->UnpublishedFinalizerSettled.store(
                    true, std::memory_order_release);
                job->FinalizeUnpublishedOnMainThread = {};
                job->State.store(JobState::Published, std::memory_order_release);
                published += 1;
            }
            else
            {
                job->State.store(JobState::Dropped, std::memory_order_release);
                unpublished.push_back(job);
                dropped += 1;
                Core::Log::Error(
                    "[JobService] Job '{}' completion publisher rejected result "
                    "type '{}'; completion dropped.",
                    job->DebugName,
                    completion.Result.TypeName());
            }
        }

        {
            std::lock_guard lock(m_State->Mutex);
            // Parked records go back to the front so they keep their place
            // ahead of results that finished later.
            if (!deferred.empty())
            {
                m_State->CompletionQueue.insert(
                    m_State->CompletionQueue.begin(),
                    std::make_move_iterator(deferred.begin()),
                    std::make_move_iterator(deferred.end()));
            }
            m_State->Stats.CompletedJobs += published;
            m_State->Stats.PublishedCompletions += published;
            m_State->Stats.DroppedCompletions += dropped;
            m_State->Stats.StaleDiscardedJobs += staleDiscarded;
            m_State->Stats.AwaitingApplyJobs =
                static_cast<std::uint64_t>(deferred.size());
            m_State->Stats.LastDrainPublished = published;
            m_State->Stats.LastDrainDropped = dropped;
            m_State->Stats.LastDrainParked = parked;
            m_State->Stats.LastDrainStaleDiscarded = staleDiscarded;
            for (const auto& job : unpublished)
                QueueUnpublishedFinalizerLocked(*m_State, job);
        }

        // Runs after publication so a dependency retired by this very drain
        // releases its dependents now, rather than costing them a extra frame.
        ReleaseSatisfiedDependencies();

        // Last, so cancellations that this drain itself produced — including
        // the dependency cancellations released just above — reconcile their
        // consumer's control state in the same drain instead of the next one.
        const std::uint64_t finalized = RunUnpublishedFinalizers();
        {
            std::lock_guard lock(m_State->Mutex);
            m_State->Stats.LastDrainFinalizedUnpublished = finalized;
        }

        return published;
    }

    JobApplyValidation JobService::ResolveApplyValidation(JobRecord& job) const
    {
        if (job.CancelRequested->load(std::memory_order_acquire))
            return JobApplyValidation::Cancelled;

        // Epoch check: a result captured before its world was replaced must
        // never mutate the replacement.
        if (job.CancellationGeneration != 0u)
        {
            std::lock_guard lock(m_State->Mutex);
            const auto it = m_State->WorldGenerations.find(job.Scope);
            const std::uint64_t current =
                it == m_State->WorldGenerations.end() ? 0u : it->second;
            if (current != 0u && current != job.CancellationGeneration)
                return JobApplyValidation::StaleWorld;
        }

        if (job.ValidateBeforeApply)
            return job.ValidateBeforeApply();

        return JobApplyValidation::Current;
    }

    void JobService::ReleaseSatisfiedDependencies()
    {
        if (!m_State)
            return;

        std::vector<std::shared_ptr<JobRecord>> ready;
        std::vector<std::shared_ptr<JobRecord>> cancelled;
        {
            std::lock_guard lock(m_State->Mutex);
            std::vector<std::shared_ptr<JobRecord>> stillPending;
            stillPending.reserve(m_State->PendingDependencies.size());

            for (auto& job : m_State->PendingDependencies)
            {
                if (!job)
                    continue;
                if (job->CancelRequested->load(std::memory_order_acquire))
                {
                    cancelled.push_back(job);
                    continue;
                }

                bool allTerminal = true;
                bool anyFailed = false;
                for (const JobToken dependency : job->DependsOn)
                {
                    const auto it = m_State->Jobs.find(dependency);
                    if (it == m_State->Jobs.end() || !it->second)
                        continue;  // reaped: treat as satisfied
                    const JobState state =
                        it->second->State.load(std::memory_order_acquire);
                    if (!IsTerminal(state))
                    {
                        allTerminal = false;
                        break;
                    }
                    if (state != JobState::Published)
                        anyFailed = true;
                }

                if (!allTerminal)
                {
                    stillPending.push_back(job);
                    continue;
                }

                if (anyFailed)
                {
                    // A chain never applies half its work: if an upstream job
                    // did not publish, its dependents are cancelled rather than
                    // run against inputs that were never produced.
                    (void)job->CancelRequested->exchange(
                        true, std::memory_order_acq_rel);
                    cancelled.push_back(job);
                    continue;
                }

                ready.push_back(job);
            }

            m_State->PendingDependencies.swap(stillPending);
            m_State->Stats.AwaitingDependencyJobs =
                static_cast<std::uint64_t>(m_State->PendingDependencies.size());
            m_State->Stats.DependencyCancelledJobs +=
                static_cast<std::uint64_t>(cancelled.size());
            m_State->Stats.CancelledJobs +=
                static_cast<std::uint64_t>(cancelled.size());
            // These never dispatched, so no worker path will ever reach them;
            // this is their only chance to owe their consumer a finalizer.
            for (const auto& job : cancelled)
                QueueUnpublishedFinalizerLocked(*m_State, job);
        }

        for (const auto& job : cancelled)
            job->State.store(JobState::Cancelled, std::memory_order_release);
        for (const auto& job : ready)
            DispatchJob(m_State, job);
    }

    void JobService::ReportProgress(const JobToken token,
                                    const JobProgress progress)
    {
        if (!token.IsValid() || !m_State)
            return;

        std::shared_ptr<JobRecord> job;
        {
            std::lock_guard lock(m_State->Mutex);
            const auto it = m_State->Jobs.find(token);
            if (it == m_State->Jobs.end())
                return;
            job = it->second;
        }
        if (!job)
            return;
        job->ProgressNormalized.store(progress.Normalized,
                                      std::memory_order_release);
        job->ProgressDeterminate.store(progress.Determinate,
                                       std::memory_order_release);
    }

    JobProgress JobService::GetProgress(const JobToken token) const
    {
        if (!token.IsValid() || !m_State)
            return {};

        std::shared_ptr<JobRecord> job;
        {
            std::lock_guard lock(m_State->Mutex);
            const auto it = m_State->Jobs.find(token);
            if (it == m_State->Jobs.end())
                return {};
            job = it->second;
        }
        if (!job)
            return {};
        return JobProgress{
            .Normalized = job->ProgressNormalized.load(std::memory_order_acquire),
            .Determinate =
                job->ProgressDeterminate.load(std::memory_order_acquire),
        };
    }

    std::uint64_t JobService::AdvanceWorldGeneration(const WorldHandle world)
    {
        if (!world.IsValid() || !m_State)
            return 0;
        std::lock_guard lock(m_State->Mutex);
        return ++m_State->WorldGenerations[world];
    }

    std::uint64_t JobService::WorldGeneration(const WorldHandle world) const
    {
        if (!world.IsValid() || !m_State)
            return 0;
        std::lock_guard lock(m_State->Mutex);
        const auto it = m_State->WorldGenerations.find(world);
        return it == m_State->WorldGenerations.end() ? 0u : it->second;
    }

    std::uint64_t JobService::ReapCompleted()
    {
        if (!m_State)
            return 0;

        std::uint64_t reaped = 0;
        std::lock_guard lock(m_State->Mutex);
        for (auto it = m_State->Jobs.begin(); it != m_State->Jobs.end();)
        {
            const std::shared_ptr<JobService::JobRecord>& job = it->second;
            if (!job ||
                (IsTerminal(job->State.load(std::memory_order_acquire)) &&
                 job->UnpublishedFinalizerSettled.load(
                     std::memory_order_acquire)))
            {
                it = m_State->Jobs.erase(it);
                reaped += 1;
            }
            else
            {
                ++it;
            }
        }
        m_State->Stats.ReapedJobs += reaped;
        return reaped;
    }

    JobServiceStats JobService::Stats() const
    {
        if (!m_State)
            return {};

        std::lock_guard lock(m_State->Mutex);
        JobServiceStats stats = m_State->Stats;
        for (const auto& [_, job] : m_State->Jobs)
        {
            if (!job)
                continue;

            switch (job->State.load(std::memory_order_acquire))
            {
            case JobState::Queued:
                stats.QueuedJobs += 1;
                stats.InFlightJobs += 1;
                break;
            case JobState::Running:
                stats.RunningJobs += 1;
                stats.InFlightJobs += 1;
                break;
            case JobState::AwaitingGate:
                stats.AwaitingGateJobs += 1;
                stats.InFlightJobs += 1;
                break;
            case JobState::Invalid:
            case JobState::Published:
            case JobState::Dropped:
            case JobState::Cancelled:
            case JobState::Rejected:
                break;
            }
        }
        return stats;
    }

    std::vector<JobSnapshot> JobService::SnapshotAll() const
    {
        if (!m_State)
            return {};

        const auto now = std::chrono::steady_clock::now();

        std::vector<JobSnapshot> snapshots;
        {
            std::lock_guard lock(m_State->Mutex);
            snapshots.reserve(m_State->Jobs.size());
            for (const auto& [token, job] : m_State->Jobs)
            {
                if (!job)
                    continue;
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - job->SubmittedAt);
                snapshots.push_back(JobSnapshot{
                    .Token = token,
                    .DebugName = job->DebugName,
                    .State = job->State.load(std::memory_order_acquire),
                    .Scope = job->Scope,
                    .Progress =
                        JobProgress{
                            .Normalized = job->ProgressNormalized.load(
                                std::memory_order_acquire),
                            .Determinate = job->ProgressDeterminate.load(
                                std::memory_order_acquire),
                        },
                    .ElapsedMilliseconds = static_cast<std::uint64_t>(
                        std::max<std::int64_t>(0, elapsed.count())),
                });
            }
        }

        // Token order, not map order: a queue view must not reshuffle its rows
        // between frames just because the hash map rehashed.
        std::sort(
            snapshots.begin(),
            snapshots.end(),
            [](const JobSnapshot& lhs, const JobSnapshot& rhs)
            {
                return lhs.Token < rhs.Token;
            });
        return snapshots;
    }

    GpuQueueParticipantHandle JobService::RegisterGpuQueueParticipant(
        GpuQueueParticipantDesc desc)
    {
        if (!m_State)
            return {};

        if (!desc.RecordFrameCommands && !desc.DrainCompletedTransfers &&
            !desc.HasInFlightWork && !desc.ShutdownAfterDeviceIdle)
        {
            return {};
        }

        std::lock_guard lock(m_State->Mutex);
        SharedState::GpuQueueParticipantRecord record{};
        record.Handle =
            GpuQueueParticipantHandle{m_State->NextGpuQueueParticipantIndex++, 1u};
        record.DebugName = std::move(desc.DebugName);
        record.Scope = desc.Scope.IsValid() ? desc.Scope : DefaultWorldHandle;
        record.RecordFrameCommands = std::move(desc.RecordFrameCommands);
        record.DrainCompletedTransfers = std::move(desc.DrainCompletedTransfers);
        record.HasInFlightWork = std::move(desc.HasInFlightWork);
        record.ShutdownAfterDeviceIdle = std::move(desc.ShutdownAfterDeviceIdle);
        m_State->GpuQueueParticipants.push_back(std::move(record));
        return m_State->GpuQueueParticipants.back().Handle;
    }

    void JobService::UnregisterGpuQueueParticipant(
        const GpuQueueParticipantHandle handle,
        std::function<void()> waitForGpuIdle)
    {
        if (!handle.IsValid() || !m_State)
            return;

        SharedState::GpuQueueParticipantRecord record{};
        {
            std::lock_guard lock(m_State->Mutex);
            const auto it = std::ranges::find_if(
                m_State->GpuQueueParticipants,
                [handle](const SharedState::GpuQueueParticipantRecord& participant) noexcept
                {
                    return participant.Handle == handle;
                });
            if (it == m_State->GpuQueueParticipants.end())
                return;

            record = std::move(*it);
            m_State->GpuQueueParticipants.erase(it);
        }

        const bool hasInFlightWork = record.HasInFlightWork
            ? record.HasInFlightWork()
            : static_cast<bool>(record.RecordFrameCommands);
        if (hasInFlightWork && waitForGpuIdle)
            waitForGpuIdle();
        if (record.ShutdownAfterDeviceIdle)
            record.ShutdownAfterDeviceIdle();
    }

    void JobService::RecordGpuQueueFrameCommands(
        RHI::ICommandContext& commandContext)
    {
        if (!m_State)
            return;

        std::vector<std::function<void(RHI::ICommandContext&)>> callbacks;
        {
            std::lock_guard lock(m_State->Mutex);
            callbacks.reserve(m_State->GpuQueueParticipants.size());
            for (const SharedState::GpuQueueParticipantRecord& participant :
                 m_State->GpuQueueParticipants)
            {
                if (participant.RecordFrameCommands)
                    callbacks.push_back(participant.RecordFrameCommands);
            }
        }

        for (const std::function<void(RHI::ICommandContext&)>& callback :
             callbacks)
        {
            callback(commandContext);
        }
    }

    std::uint64_t JobService::DrainGpuQueueCompletedTransfers()
    {
        if (!m_State)
            return 0;

        std::vector<std::function<void()>> callbacks;
        {
            std::lock_guard lock(m_State->Mutex);
            callbacks.reserve(m_State->GpuQueueParticipants.size());
            for (const SharedState::GpuQueueParticipantRecord& participant :
                 m_State->GpuQueueParticipants)
            {
                if (participant.DrainCompletedTransfers)
                    callbacks.push_back(participant.DrainCompletedTransfers);
            }
        }

        for (const std::function<void()>& callback : callbacks)
        {
            callback();
        }
        return static_cast<std::uint64_t>(callbacks.size());
    }

    bool JobService::HasGpuQueueWork() const
    {
        if (!m_State)
            return false;

        std::vector<std::function<bool()>> probes;
        bool hasUnconditionalFrameParticipant = false;
        {
            std::lock_guard lock(m_State->Mutex);
            probes.reserve(m_State->GpuQueueParticipants.size());
            for (const SharedState::GpuQueueParticipantRecord& participant :
                 m_State->GpuQueueParticipants)
            {
                if (participant.HasInFlightWork)
                {
                    probes.push_back(participant.HasInFlightWork);
                }
                else if (participant.RecordFrameCommands)
                {
                    hasUnconditionalFrameParticipant = true;
                }
            }
        }

        if (hasUnconditionalFrameParticipant)
            return true;

        for (const std::function<bool()>& probe : probes)
        {
            if (probe())
                return true;
        }
        return false;
    }

    std::uint64_t JobService::ShutdownGpuQueueParticipants(
        std::function<void()> waitForGpuIdle)
    {
        if (!m_State)
            return 0;

        std::vector<GpuQueueParticipantHandle> handles;
        {
            std::lock_guard lock(m_State->Mutex);
            handles.reserve(m_State->GpuQueueParticipants.size());
            for (const SharedState::GpuQueueParticipantRecord& participant :
                 m_State->GpuQueueParticipants)
            {
                handles.push_back(participant.Handle);
            }
        }

        for (auto it = handles.rbegin(); it != handles.rend(); ++it)
        {
            UnregisterGpuQueueParticipant(*it, waitForGpuIdle);
        }
        return static_cast<std::uint64_t>(handles.size());
    }
}
