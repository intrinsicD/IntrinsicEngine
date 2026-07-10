module;

#include <algorithm>
#include <atomic>
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
                return true;
            case JobState::Invalid:
            case JobState::Queued:
            case JobState::Running:
            case JobState::AwaitingGate:
                return false;
            }
            return false;
        }
    }

    struct JobService::SharedState
    {
        struct JobRecord
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
        };

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
        JobServiceStats Stats{};
    };

    JobService::JobService()
        : m_State(std::make_shared<SharedState>())
    {
    }

    JobService::~JobService() = default;

    bool JobCancellation::IsCancelled() const noexcept
    {
        return m_Flag && m_Flag->load(std::memory_order_acquire);
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

        auto job = std::make_shared<SharedState::JobRecord>();
        {
            std::lock_guard lock(m_State->Mutex);
            job->Token = JobToken{m_State->NextTokenIndex++, 1u};
            job->Scope = desc.Scope.IsValid() ? desc.Scope : DefaultWorldHandle;
            job->Target = desc.Target;
            job->DebugName = std::move(desc.DebugName);
            job->Work = std::move(desc.Work);
            job->PublishCompletion = std::move(desc.PublishCompletion);
            m_State->Jobs.emplace(job->Token, job);
            m_State->Stats.SubmittedJobs += 1;
        }

        const std::shared_ptr<SharedState> state = m_State;
        Core::Tasks::Scheduler::Dispatch([state, job]() mutable
        {
            if (job->CancelRequested->load(std::memory_order_acquire))
            {
                job->State.store(JobState::Cancelled, std::memory_order_release);
                return;
            }

            job->State.store(JobState::Running, std::memory_order_release);

            JobResultEnvelope result =
                job->Work(JobCancellation(job->CancelRequested));

            if (job->CancelRequested->load(std::memory_order_acquire))
            {
                job->State.store(JobState::Cancelled, std::memory_order_release);
                return;
            }

            if (!result.IsValid())
            {
                job->State.store(JobState::Dropped, std::memory_order_release);
                std::lock_guard lock(state->Mutex);
                state->Stats.DroppedCompletions += 1;
                Core::Log::Error(
                    "[JobService] Job '{}' produced an empty result envelope; "
                    "completion dropped.",
                    job->DebugName);
                return;
            }

            {
                std::lock_guard lock(state->Mutex);
                // BUG-067: publish AwaitingGate under the same lock as the
                // enqueue, before the completion becomes visible to the drain.
                // Storing it after the unlock lets a same-tick DrainCompletions
                // publish/drop the job and set a terminal state that this store
                // would then clobber back to AwaitingGate, wedging the job
                // non-terminal forever (record leak, phantom stats, hung poller).
                job->State.store(JobState::AwaitingGate, std::memory_order_release);
                state->CompletionQueue.push_back(SharedState::CompletionRecord{
                    .Job = job,
                    .Result = std::move(result),
                });
            }
        });

        return job->Token;
    }

    bool JobService::Cancel(const JobToken token)
    {
        if (!token.IsValid() || !m_State)
            return false;

        std::shared_ptr<SharedState::JobRecord> job;
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
        return IsTerminal(GetState(token));
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
        if (!m_State)
            return 0;

        std::vector<SharedState::CompletionRecord> batch;
        {
            std::lock_guard lock(m_State->Mutex);
            batch.swap(m_State->CompletionQueue);
            m_State->Stats.CompletionDrains += 1;
            m_State->Stats.LastDrainFinished =
                static_cast<std::uint64_t>(batch.size());
            m_State->Stats.LastDrainPublished = 0;
            m_State->Stats.LastDrainDropped = 0;
        }

        std::uint64_t published = 0;
        std::uint64_t dropped = 0;
        for (const SharedState::CompletionRecord& completion : batch)
        {
            const std::shared_ptr<SharedState::JobRecord>& job = completion.Job;
            if (!job)
            {
                dropped += 1;
                continue;
            }

            if (job->CancelRequested->load(std::memory_order_acquire))
            {
                job->State.store(JobState::Cancelled, std::memory_order_release);
                dropped += 1;
                continue;
            }

            const bool didPublish =
                job->PublishCompletion(events, completion.Result);
            if (didPublish)
            {
                job->State.store(JobState::Published, std::memory_order_release);
                published += 1;
            }
            else
            {
                job->State.store(JobState::Dropped, std::memory_order_release);
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
            m_State->Stats.CompletedJobs += published;
            m_State->Stats.PublishedCompletions += published;
            m_State->Stats.DroppedCompletions += dropped;
            m_State->Stats.LastDrainPublished = published;
            m_State->Stats.LastDrainDropped = dropped;
        }
        return published;
    }

    std::uint64_t JobService::ReapCompleted()
    {
        if (!m_State)
            return 0;

        std::uint64_t reaped = 0;
        std::lock_guard lock(m_State->Mutex);
        for (auto it = m_State->Jobs.begin(); it != m_State->Jobs.end();)
        {
            const std::shared_ptr<SharedState::JobRecord>& job = it->second;
            if (!job ||
                IsTerminal(job->State.load(std::memory_order_acquire)))
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
