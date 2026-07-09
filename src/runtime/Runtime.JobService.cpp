module;

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Runtime.JobService;

import Extrinsic.Core.Logging;
import Extrinsic.Core.Tasks;
import Extrinsic.Runtime.KernelEvents;

namespace Extrinsic::Runtime
{
    namespace
    {
        enum class JobState : std::uint8_t
        {
            Queued,
            Running,
            Finished,
            Publishing,
            Completed,
            Cancelled,
            DroppedWorldScope,
        };

        [[nodiscard]] bool IsTerminal(const JobState state) noexcept
        {
            return state == JobState::Completed ||
                   state == JobState::Cancelled ||
                   state == JobState::DroppedWorldScope;
        }
    }

    JobCancellation::JobCancellation(
        std::shared_ptr<std::atomic_bool> flag) noexcept
        : m_Flag(std::move(flag))
    {
    }

    bool JobCancellation::IsCancellationRequested() const noexcept
    {
        return m_Flag != nullptr &&
               m_Flag->load(std::memory_order_acquire);
    }

    struct JobService::Impl
    {
        struct SharedJob
        {
            JobToken Token{};
            WorldHandle Scope{};
            std::string DebugName{};
            std::shared_ptr<std::atomic_bool> Cancelled{
                std::make_shared<std::atomic_bool>(false)};
            std::move_only_function<std::shared_ptr<void>(const JobCancellation&)>
                Work{};
            std::move_only_function<void(EventBus&, std::shared_ptr<void>)>
                PublishCompletion{};
            std::shared_ptr<void> Result{};
            bool Reaped{false};
        };

        struct Record
        {
            std::uint32_t Generation{1};
            JobState State{JobState::Queued};
            std::shared_ptr<SharedJob> Job{};
        };

        mutable std::mutex Mutex{};
        std::vector<Record> Records{};
        std::vector<JobToken> CompletionQueue{};
        JobServiceStats Stats{};

        [[nodiscard]] bool ResolveLocked(const JobToken token,
                                         std::uint32_t& outIndex) const noexcept
        {
            if (!token.IsValid() || token.Index >= Records.size())
            {
                return false;
            }
            if (Records[token.Index].Generation != token.Generation)
            {
                return false;
            }
            outIndex = token.Index;
            return true;
        }

        [[nodiscard]] JobServiceStats BuildStatsLocked() const
        {
            JobServiceStats result = Stats;
            result.InFlight = 0;
            for (const Record& record : Records)
            {
                if (!IsTerminal(record.State) && !record.Job->Reaped)
                {
                    ++result.InFlight;
                }
            }
            result.PendingCompletions =
                static_cast<std::uint64_t>(CompletionQueue.size());
            return result;
        }

        [[nodiscard]] bool MarkRunning(const JobToken token)
        {
            std::lock_guard lock(Mutex);

            std::uint32_t index = 0;
            if (!ResolveLocked(token, index))
            {
                return false;
            }

            Record& record = Records[index];
            if (IsTerminal(record.State))
            {
                return false;
            }

            record.State = JobState::Running;
            Stats.Launched += 1;
            return true;
        }

        void MarkFinished(JobToken token, std::shared_ptr<void> result)
        {
            std::lock_guard lock(Mutex);

            std::uint32_t index = 0;
            if (!ResolveLocked(token, index))
            {
                return;
            }

            Record& record = Records[index];
            if (IsTerminal(record.State))
            {
                return;
            }

            record.Job->Result = std::move(result);
            record.State = JobState::Finished;
            CompletionQueue.push_back(token);
            Stats.WorkerFinished += 1;
        }

        void RequestCancelLocked(Record& record)
        {
            if (IsTerminal(record.State) || record.State == JobState::Publishing)
            {
                return;
            }

            const bool alreadyCancelled =
                record.Job->Cancelled->exchange(true, std::memory_order_acq_rel);
            if (!alreadyCancelled)
            {
                Stats.CancelRequested += 1;
            }
        }
    };

    JobService::JobService()
        : m_Impl(std::make_shared<Impl>())
    {
    }

    JobService::~JobService() = default;

    JobService::JobService(JobService&&) noexcept = default;
    JobService& JobService::operator=(JobService&&) noexcept = default;

    JobToken JobService::Submit(JobDesc desc)
    {
        if (m_Impl == nullptr)
        {
            return {};
        }

        if (desc.Target != JobTarget::CpuPool)
        {
            std::lock_guard lock(m_Impl->Mutex);
            m_Impl->Stats.UnsupportedTarget += 1;
            Core::Log::Warn(
                "[JobService] Rejected job '{}' for unsupported target.",
                desc.DebugName);
            return {};
        }

        if (!desc.Work || !desc.PublishCompletion)
        {
            Core::Log::Error(
                "[JobService] Rejected job '{}' with incomplete callbacks.",
                desc.DebugName);
            return {};
        }

        if (!desc.Scope.IsValid())
        {
            Core::Log::Error(
                "[JobService] Rejected job '{}' without a valid world scope.",
                desc.DebugName);
            return {};
        }

        if (!Core::Tasks::Scheduler::IsInitialized())
        {
            Core::Log::Error(
                "[JobService] Rejected job '{}' because the scheduler is not "
                "initialized.",
                desc.DebugName);
            return {};
        }

        auto job = std::make_shared<Impl::SharedJob>();
        job->Scope = desc.Scope;
        job->DebugName = std::move(desc.DebugName);
        job->Work = std::move(desc.Work);
        job->PublishCompletion = std::move(desc.PublishCompletion);

        JobToken token{};
        {
            std::lock_guard lock(m_Impl->Mutex);
            const auto index = static_cast<std::uint32_t>(m_Impl->Records.size());
            token = JobToken{index, 1u};
            job->Token = token;
            m_Impl->Records.push_back(Impl::Record{
                .Generation = token.Generation,
                .State = JobState::Queued,
                .Job = job});
            m_Impl->Stats.Submitted += 1;
        }

        std::weak_ptr<Impl> weakImpl{m_Impl};
        Core::Tasks::Scheduler::Dispatch(
            [weakImpl = std::move(weakImpl), job]() mutable
            {
                const std::shared_ptr<Impl> impl = weakImpl.lock();
                if (impl == nullptr)
                {
                    return;
                }

                if (!impl->MarkRunning(job->Token))
                {
                    return;
                }

                std::shared_ptr<void> result{};
                const JobCancellation cancellation{job->Cancelled};
                if (!cancellation.IsCancellationRequested())
                {
                    result = job->Work(cancellation);
                }

                impl->MarkFinished(job->Token, std::move(result));
            });

        return token;
    }

    void JobService::Cancel(JobToken token)
    {
        if (m_Impl == nullptr || !token.IsValid())
        {
            return;
        }

        std::lock_guard lock(m_Impl->Mutex);
        std::uint32_t index = 0;
        if (!m_Impl->ResolveLocked(token, index))
        {
            return;
        }
        m_Impl->RequestCancelLocked(m_Impl->Records[index]);
    }

    bool JobService::IsComplete(JobToken token) const
    {
        if (m_Impl == nullptr || !token.IsValid())
        {
            return false;
        }

        std::lock_guard lock(m_Impl->Mutex);
        std::uint32_t index = 0;
        if (!m_Impl->ResolveLocked(token, index))
        {
            return false;
        }
        return IsTerminal(m_Impl->Records[index].State);
    }

    std::uint32_t JobService::CancelAllForWorld(WorldHandle world)
    {
        if (m_Impl == nullptr || !world.IsValid())
        {
            return 0;
        }

        std::uint32_t cancelled = 0;
        std::lock_guard lock(m_Impl->Mutex);
        for (Impl::Record& record : m_Impl->Records)
        {
            if (record.Job->Scope != world ||
                IsTerminal(record.State) ||
                record.State == JobState::Publishing)
            {
                continue;
            }

            const bool wasCancelled =
                record.Job->Cancelled->load(std::memory_order_acquire);
            m_Impl->RequestCancelLocked(record);
            if (!wasCancelled)
            {
                ++cancelled;
            }
        }
        return cancelled;
    }

    void JobService::DrainCompletions(EventBus& events)
    {
        if (m_Impl == nullptr)
        {
            return;
        }

        std::vector<JobToken> completions;
        {
            std::lock_guard lock(m_Impl->Mutex);
            completions.swap(m_Impl->CompletionQueue);
            m_Impl->Stats.LastDrainPublished = 0;
            m_Impl->Stats.LastDrainDropped = 0;
        }

        std::uint64_t published = 0;
        std::uint64_t dropped = 0;

        for (const JobToken token : completions)
        {
            std::move_only_function<void(EventBus&, std::shared_ptr<void>)>
                publish{};
            std::shared_ptr<void> result{};
            bool dropCancelled = false;
            bool dropWorldScope = false;

            {
                std::lock_guard lock(m_Impl->Mutex);
                std::uint32_t index = 0;
                if (!m_Impl->ResolveLocked(token, index))
                {
                    continue;
                }

                Impl::Record& record = m_Impl->Records[index];
                if (IsTerminal(record.State))
                {
                    continue;
                }

                if (record.Job->Cancelled->load(std::memory_order_acquire))
                {
                    record.State = JobState::Cancelled;
                    record.Job->Result.reset();
                    dropCancelled = true;
                }
                else if (!record.Job->Scope.IsValid())
                {
                    record.State = JobState::DroppedWorldScope;
                    record.Job->Result.reset();
                    dropWorldScope = true;
                }
                else
                {
                    record.State = JobState::Publishing;
                    result = std::move(record.Job->Result);
                    publish = std::move(record.Job->PublishCompletion);
                }
            }

            if (dropCancelled || dropWorldScope)
            {
                ++dropped;
                std::lock_guard lock(m_Impl->Mutex);
                if (dropCancelled)
                {
                    m_Impl->Stats.DroppedCancelled += 1;
                }
                if (dropWorldScope)
                {
                    m_Impl->Stats.DroppedWorldScope += 1;
                }
                continue;
            }

            if (publish)
            {
                publish(events, std::move(result));
            }

            {
                std::lock_guard lock(m_Impl->Mutex);
                std::uint32_t index = 0;
                if (m_Impl->ResolveLocked(token, index))
                {
                    m_Impl->Records[index].State = JobState::Completed;
                    m_Impl->Stats.PublishedCompletions += 1;
                    ++published;
                }
            }
        }

        {
            std::lock_guard lock(m_Impl->Mutex);
            m_Impl->Stats.LastDrainPublished = published;
            m_Impl->Stats.LastDrainDropped = dropped;
        }
    }

    std::uint32_t JobService::ReapCompleted()
    {
        if (m_Impl == nullptr)
        {
            return 0;
        }

        std::uint32_t reaped = 0;
        std::lock_guard lock(m_Impl->Mutex);
        for (Impl::Record& record : m_Impl->Records)
        {
            if (!record.Job->Reaped && IsTerminal(record.State))
            {
                record.Job->Reaped = true;
                ++reaped;
            }
        }
        m_Impl->Stats.Reaped += reaped;
        return reaped;
    }

    JobServiceStats JobService::Stats() const
    {
        if (m_Impl == nullptr)
        {
            return {};
        }

        std::lock_guard lock(m_Impl->Mutex);
        return m_Impl->BuildStatsLocked();
    }

    std::size_t JobService::PendingCompletionCount() const
    {
        if (m_Impl == nullptr)
        {
            return 0;
        }

        std::lock_guard lock(m_Impl->Mutex);
        return m_Impl->CompletionQueue.size();
    }
}
