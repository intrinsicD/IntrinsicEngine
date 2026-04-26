module;

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
#include <variant>

module Extrinsic.Runtime.StreamingExecutor;

import Extrinsic.Core.Tasks;
import Extrinsic.Core.Dag.Scheduler:Types;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

        [[nodiscard]] int PriorityRank(const Core::Dag::TaskPriority priority)
        {
            return static_cast<int>(priority);
        }

        [[nodiscard]] StreamingResult CancelledResult()
        {
            return std::unexpected(Core::ErrorCode::InvalidState);
        }

        [[nodiscard]] bool IsUploadRequest(const StreamingResult& result)
        {
            return result.has_value() && std::holds_alternative<StreamingGpuUploadRequest>(*result);
        }
    }

    struct StreamingExecutor::Impl
    {
        struct TaskRecord
        {
            std::uint32_t Generation = 1;
            std::uint32_t RemainingDeps = 0;
            StreamingTaskState State = StreamingTaskState::Pending;
            StreamingTaskDesc Desc{};
            std::vector<std::uint32_t> Dependents{};
            std::optional<StreamingResult> Result{};
            bool Launched = false;
        };

        struct Completion
        {
            std::uint32_t Index = kInvalidIndex;
            std::uint32_t Generation = 0;
            StreamingResult Result{};
        };

        mutable std::mutex Mutex{};
        std::condition_variable CompletionCv{};
        std::vector<TaskRecord> Tasks{};
        std::unordered_map<std::uint64_t, std::uint32_t> HandleToIndex{};
        std::deque<Completion> Completions{};
        std::deque<std::uint32_t> ReadyForApply{};
        std::uint32_t RunningCount = 0;
        bool IsShutdown = false;

        [[nodiscard]] static std::uint64_t HandleKey(const StreamingTaskHandle handle)
        {
            return (static_cast<std::uint64_t>(handle.Generation) << 32u) | handle.Index;
        }

        [[nodiscard]] std::optional<std::uint32_t> Resolve(StreamingTaskHandle handle) const
        {
            const auto it = HandleToIndex.find(HandleKey(handle));
            if (it == HandleToIndex.end())
            {
                return std::nullopt;
            }
            return it->second;
        }

        [[nodiscard]] std::optional<std::uint32_t> FindNextReadyIndex() const
        {
            std::optional<std::uint32_t> best{};

            for (std::uint32_t index = 0; index < Tasks.size(); ++index)
            {
                const auto& task = Tasks[index];
                if (task.State != StreamingTaskState::Pending || task.Launched || task.RemainingDeps != 0)
                {
                    continue;
                }

                if (!best.has_value())
                {
                    best = index;
                    continue;
                }

                const auto& currentBest = Tasks[*best];
                const auto rank = PriorityRank(task.Desc.Priority);
                const auto bestRank = PriorityRank(currentBest.Desc.Priority);
                if (rank < bestRank)
                {
                    best = index;
                }
            }

            return best;
        }

        void FinalizeTaskLocked(const std::uint32_t index)
        {
            auto& task = Tasks[index];
            if (task.Result.has_value() && !task.Result->has_value())
            {
                task.State = (task.State == StreamingTaskState::Cancelled)
                    ? StreamingTaskState::Cancelled
                    : StreamingTaskState::Failed;
            }
            else
            {
                task.State = (task.State == StreamingTaskState::Cancelled)
                    ? StreamingTaskState::Cancelled
                    : StreamingTaskState::Complete;
            }

            for (const auto dependentIndex : task.Dependents)
            {
                auto& dependent = Tasks[dependentIndex];
                if (dependent.RemainingDeps > 0)
                {
                    --dependent.RemainingDeps;
                }
            }
        }
    };

    StreamingExecutor::StreamingExecutor()
        : m_Impl(new Impl())
    {
    }

    StreamingExecutor::~StreamingExecutor()
    {
        ShutdownAndDrain();
        delete m_Impl;
        m_Impl = nullptr;
    }

    StreamingTaskHandle StreamingExecutor::Submit(StreamingTaskDesc desc)
    {
        if (!desc.Execute)
        {
            return {};
        }

        std::scoped_lock lock(m_Impl->Mutex);
        if (m_Impl->IsShutdown)
        {
            return {};
        }

        const std::uint32_t index = static_cast<std::uint32_t>(m_Impl->Tasks.size());
        Impl::TaskRecord record{};
        record.Desc = std::move(desc);
        record.Desc.EstimatedCost = std::max<std::uint32_t>(1u, record.Desc.EstimatedCost);

        for (const auto dependency : record.Desc.DependsOn)
        {
            if (const auto dependencyIndex = m_Impl->Resolve(dependency); dependencyIndex.has_value())
            {
                auto& parent = m_Impl->Tasks[*dependencyIndex];
                if (parent.State != StreamingTaskState::Complete && parent.State != StreamingTaskState::Failed &&
                    parent.State != StreamingTaskState::Cancelled)
                {
                    ++record.RemainingDeps;
                    parent.Dependents.push_back(index);
                }
            }
        }

        const auto generation = record.Generation;
        m_Impl->Tasks.push_back(std::move(record));

        StreamingTaskHandle handle{index, generation};
        m_Impl->HandleToIndex.emplace(Impl::HandleKey(handle), index);
        return handle;
    }

    void StreamingExecutor::Cancel(const StreamingTaskHandle handle)
    {
        std::scoped_lock lock(m_Impl->Mutex);
        const auto index = m_Impl->Resolve(handle);
        if (!index.has_value())
        {
            return;
        }

        auto& task = m_Impl->Tasks[*index];
        if (task.State == StreamingTaskState::Complete || task.State == StreamingTaskState::Failed ||
            task.State == StreamingTaskState::Cancelled)
        {
            return;
        }

        task.State = StreamingTaskState::Cancelled;
        task.Result = CancelledResult();
        if (!task.Launched)
        {
            m_Impl->FinalizeTaskLocked(*index);
        }
    }

    void StreamingExecutor::PumpBackground(const std::uint32_t maxLaunches)
    {
        std::uint32_t launches = 0;

        while (launches < maxLaunches)
        {
            std::uint32_t launchIndex = kInvalidIndex;
            std::uint32_t launchGeneration = 0;
            std::move_only_function<StreamingResult()> executeFn{};

            {
                std::scoped_lock lock(m_Impl->Mutex);
                if (m_Impl->IsShutdown)
                {
                    return;
                }

                const auto next = m_Impl->FindNextReadyIndex();
                if (!next.has_value())
                {
                    return;
                }

                auto& task = m_Impl->Tasks[*next];
                launchIndex = *next;
                launchGeneration = task.Generation;
                task.Launched = true;
                task.State = StreamingTaskState::Running;
                executeFn = std::move(task.Desc.Execute);
                ++m_Impl->RunningCount;
            }

            auto runTask = [impl = m_Impl, index = launchIndex, generation = launchGeneration,
                            fn = std::move(executeFn)]() mutable
            {
                StreamingResult result = std::unexpected(Core::ErrorCode::Unknown);
                if (fn)
                {
                    result = fn();
                }

                {
                    std::scoped_lock completionLock(impl->Mutex);
                    impl->Completions.push_back(Impl::Completion{
                        .Index = index,
                        .Generation = generation,
                        .Result = std::move(result),
                    });
                }
                impl->CompletionCv.notify_all();
            };

            if (Core::Tasks::Scheduler::IsInitialized())
            {
                Core::Tasks::Scheduler::Dispatch(std::move(runTask));
            }
            else
            {
                runTask();
            }

            ++launches;
        }
    }

    void StreamingExecutor::DrainCompletions()
    {
        std::deque<Impl::Completion> drained{};
        {
            std::scoped_lock lock(m_Impl->Mutex);
            drained.swap(m_Impl->Completions);
        }

        for (auto& completion : drained)
        {
            std::scoped_lock lock(m_Impl->Mutex);
            if (completion.Index >= m_Impl->Tasks.size())
            {
                continue;
            }

            auto& task = m_Impl->Tasks[completion.Index];
            if (task.Generation != completion.Generation)
            {
                continue;
            }

            if (m_Impl->RunningCount > 0)
            {
                --m_Impl->RunningCount;
            }

            if (task.State == StreamingTaskState::Cancelled)
            {
                if (!task.Result.has_value())
                {
                    task.Result = CancelledResult();
                }
                m_Impl->FinalizeTaskLocked(completion.Index);
                continue;
            }

            task.Result = std::move(completion.Result);
            if (!task.Result->has_value())
            {
                m_Impl->FinalizeTaskLocked(completion.Index);
                continue;
            }

            if (task.Desc.ApplyOnMainThread && IsUploadRequest(*task.Result))
            {
                task.State = StreamingTaskState::WaitingForGpuUpload;
                m_Impl->ReadyForApply.push_back(completion.Index);
            }
            else
            {
                m_Impl->FinalizeTaskLocked(completion.Index);
            }
        }
    }

    void StreamingExecutor::ApplyMainThreadResults()
    {
        std::deque<std::uint32_t> ready{};
        {
            std::scoped_lock lock(m_Impl->Mutex);
            ready.swap(m_Impl->ReadyForApply);
        }

        for (const auto index : ready)
        {
            std::move_only_function<void(StreamingResult&&)> apply{};
            StreamingResult result = std::unexpected(Core::ErrorCode::Unknown);
            {
                std::scoped_lock lock(m_Impl->Mutex);
                if (index >= m_Impl->Tasks.size())
                {
                    continue;
                }

                auto& task = m_Impl->Tasks[index];
                if (!task.Result.has_value())
                {
                    continue;
                }

                apply = std::move(task.Desc.ApplyOnMainThread);
                result = std::move(*task.Result);
            }

            if (apply)
            {
                apply(std::move(result));
            }

            {
                std::scoped_lock lock(m_Impl->Mutex);
                if (index < m_Impl->Tasks.size())
                {
                    m_Impl->FinalizeTaskLocked(index);
                }
            }
        }
    }

    void StreamingExecutor::ShutdownAndDrain()
    {
        if (!m_Impl)
        {
            return;
        }

        {
            std::scoped_lock lock(m_Impl->Mutex);
            if (m_Impl->IsShutdown)
            {
                return;
            }
            m_Impl->IsShutdown = true;

            for (auto& task : m_Impl->Tasks)
            {
                if (task.State == StreamingTaskState::Pending)
                {
                    task.State = StreamingTaskState::Cancelled;
                    task.Result = CancelledResult();
                }
            }
        }

        while (true)
        {
            DrainCompletions();
            ApplyMainThreadResults();

            std::unique_lock waitLock(m_Impl->Mutex);
            if (m_Impl->RunningCount == 0)
            {
                break;
            }
            m_Impl->CompletionCv.wait_for(waitLock, std::chrono::milliseconds(1));
        }
    }

    StreamingTaskState StreamingExecutor::GetState(const StreamingTaskHandle handle) const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        const auto index = m_Impl->Resolve(handle);
        if (!index.has_value())
        {
            return StreamingTaskState::Cancelled;
        }

        return m_Impl->Tasks[*index].State;
    }
}
