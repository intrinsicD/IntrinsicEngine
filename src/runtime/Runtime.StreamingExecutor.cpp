module;

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <expected>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
#include <variant>

module Extrinsic.Runtime.StreamingExecutor;

import Extrinsic.Core.Tasks;
import Extrinsic.Core.Dag.Scheduler;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

        [[nodiscard]] int PriorityRank(const Core::Dag::TaskPriority priority)
        {
            return static_cast<int>(priority);
        }

        constexpr std::size_t kPriorityQueueCount =
            static_cast<std::size_t>(Core::Dag::TaskPriority::Background) + 1u;

        [[nodiscard]] std::size_t PriorityQueueIndex(
            const Core::Dag::TaskPriority priority)
        {
            const int rank = PriorityRank(priority);
            if (rank <= 0)
            {
                return 0u;
            }

            return std::min<std::size_t>(
                static_cast<std::size_t>(rank),
                kPriorityQueueCount - 1u);
        }

        [[nodiscard]] std::uint32_t NextGeneration(std::uint32_t generation) noexcept
        {
            ++generation;
            return generation == 0u ? 1u : generation;
        }

        [[nodiscard]] StreamingResult CancelledResult()
        {
            return std::unexpected(Core::ErrorCode::InvalidState);
        }

        [[nodiscard]] bool IsUploadRequest(const StreamingResult& result)
        {
            return result.has_value() && std::holds_alternative<StreamingGpuUploadRequest>(*result);
        }

        [[nodiscard]] bool IsReadbackRequest(const StreamingResult& result)
        {
            return result.has_value() && std::holds_alternative<StreamingReadbackRequest>(*result);
        }
    }

    struct StreamingExecutor::Impl
    {
        struct TaskRecord
        {
            std::uint32_t Generation = 1;
            std::uint32_t RemainingDeps = 0;
            std::uint64_t CancellationGeneration = 0;
            StreamingTaskState State = StreamingTaskState::Pending;
            StreamingTaskDesc Desc{};
            std::vector<StreamingTaskHandle> Dependents{};
            std::optional<StreamingResult> Result{};
            bool Launched = false;
            bool QueuedReady = false;
            bool Finalized = false;
            bool Reusable = false;
        };

        struct Completion
        {
            std::uint32_t Index = kInvalidIndex;
            std::uint32_t Generation = 0;
            std::uint64_t CancellationGeneration = 0;
            StreamingResult Result{};
        };

        struct ReadyEntry
        {
            std::uint32_t Index = kInvalidIndex;
            std::uint32_t Generation = 0;
        };

        mutable std::mutex Mutex{};
        std::condition_variable CompletionCv{};
        std::vector<TaskRecord> Tasks{};
        std::unordered_map<std::uint64_t, std::uint32_t> HandleToIndex{};
        std::vector<std::uint32_t> FreeList{};
        std::deque<Completion> Completions{};
        std::array<std::deque<ReadyEntry>, kPriorityQueueCount> ReadyQueues{};
        std::deque<ReadyEntry> ReadyForApply{};
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

            const std::uint32_t index = it->second;
            if (index >= Tasks.size() ||
                Tasks[index].Generation != handle.Generation)
            {
                return std::nullopt;
            }
            return index;
        }

        void EnqueueReadyLocked(const std::uint32_t index)
        {
            if (index >= Tasks.size())
            {
                return;
            }

            TaskRecord& task = Tasks[index];
            if (task.Reusable ||
                task.Finalized ||
                task.QueuedReady ||
                task.State != StreamingTaskState::Pending ||
                task.Launched ||
                task.RemainingDeps != 0u)
            {
                return;
            }

            task.QueuedReady = true;
            ReadyQueues[PriorityQueueIndex(task.Desc.Priority)].push_back(
                ReadyEntry{.Index = index, .Generation = task.Generation});
        }

        [[nodiscard]] std::optional<std::uint32_t> PopNextReadyIndexLocked()
        {
            for (auto& queue : ReadyQueues)
            {
                while (!queue.empty())
                {
                    const ReadyEntry ready = queue.front();
                    queue.pop_front();

                    if (ready.Index >= Tasks.size())
                    {
                        continue;
                    }

                    TaskRecord& task = Tasks[ready.Index];
                    if (task.Generation != ready.Generation)
                    {
                        continue;
                    }

                    task.QueuedReady = false;
                    if (task.Reusable ||
                        task.Finalized ||
                        task.State != StreamingTaskState::Pending ||
                        task.Launched ||
                        task.RemainingDeps != 0u)
                    {
                        continue;
                    }

                    return ready.Index;
                }
            }

            return std::nullopt;
        }

        void ReleaseReusableSlotLocked(const std::uint32_t index)
        {
            if (index >= Tasks.size())
            {
                return;
            }

            TaskRecord& task = Tasks[index];
            if (task.Reusable || task.Launched)
            {
                return;
            }

            task.Desc = StreamingTaskDesc{};
            task.Dependents.clear();
            task.Result.reset();
            task.RemainingDeps = 0u;
            task.CancellationGeneration = 0u;
            task.QueuedReady = false;
            task.Reusable = true;
            FreeList.push_back(index);
        }

        void FinalizeTaskLocked(const std::uint32_t index)
        {
            if (index >= Tasks.size())
            {
                return;
            }

            auto& task = Tasks[index];
            if (task.Finalized)
            {
                if (!task.Launched)
                {
                    ReleaseReusableSlotLocked(index);
                }
                return;
            }

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
                if (dependentIndex.Index >= Tasks.size())
                {
                    continue;
                }

                auto& dependent = Tasks[dependentIndex.Index];
                if (dependent.Generation != dependentIndex.Generation ||
                    dependent.Finalized ||
                    dependent.Reusable)
                {
                    continue;
                }

                if (dependent.RemainingDeps > 0)
                {
                    --dependent.RemainingDeps;
                    if (dependent.RemainingDeps == 0u)
                    {
                        EnqueueReadyLocked(dependentIndex.Index);
                    }
                }
            }

            task.Dependents.clear();
            task.Finalized = true;
            if (!task.Launched)
            {
                ReleaseReusableSlotLocked(index);
            }
        }

        [[nodiscard]] std::uint32_t AllocateSlotLocked(TaskRecord& record)
        {
            if (FreeList.empty())
            {
                const std::uint32_t index =
                    static_cast<std::uint32_t>(Tasks.size());
                record.Generation = 1u;
                Tasks.push_back(std::move(record));
                return index;
            }

            const std::uint32_t index = FreeList.back();
            FreeList.pop_back();
            TaskRecord& slot = Tasks[index];
            HandleToIndex.erase(HandleKey(StreamingTaskHandle{
                index,
                slot.Generation,
            }));

            record.Generation = NextGeneration(slot.Generation);
            slot = std::move(record);
            return index;
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

        Impl::TaskRecord record{};
        record.Desc = std::move(desc);
        record.Desc.EstimatedCost = std::max<std::uint32_t>(1u, record.Desc.EstimatedCost);
        record.CancellationGeneration = record.Desc.CancellationGeneration;

        const std::uint32_t index = m_Impl->FreeList.empty()
            ? static_cast<std::uint32_t>(m_Impl->Tasks.size())
            : m_Impl->FreeList.back();
        record.Generation = m_Impl->FreeList.empty()
            ? 1u
            : NextGeneration(m_Impl->Tasks[index].Generation);
        const StreamingTaskHandle handle{index, record.Generation};

        for (const auto dependency : record.Desc.DependsOn)
        {
            if (const auto dependencyIndex = m_Impl->Resolve(dependency); dependencyIndex.has_value())
            {
                auto& parent = m_Impl->Tasks[*dependencyIndex];
                if (parent.State != StreamingTaskState::Complete && parent.State != StreamingTaskState::Failed &&
                    parent.State != StreamingTaskState::Cancelled)
                {
                    ++record.RemainingDeps;
                    parent.Dependents.push_back(handle);
                }
            }
        }

        const std::uint32_t allocatedIndex = m_Impl->AllocateSlotLocked(record);
        if (allocatedIndex != index)
        {
            return {};
        }
        m_Impl->HandleToIndex.emplace(Impl::HandleKey(handle), index);
        m_Impl->EnqueueReadyLocked(index);
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
        ++task.CancellationGeneration;
        m_Impl->FinalizeTaskLocked(*index);
    }

    void StreamingExecutor::PumpBackground(const std::uint32_t maxLaunches)
    {
        std::uint32_t launches = 0;

        while (launches < maxLaunches)
        {
            std::uint32_t launchIndex = kInvalidIndex;
            std::uint32_t launchGeneration = 0;
            std::uint64_t launchCancellationGeneration = 0;
            std::move_only_function<StreamingResult()> executeFn{};

            {
                std::scoped_lock lock(m_Impl->Mutex);
                if (m_Impl->IsShutdown)
                {
                    return;
                }

                const auto next = m_Impl->PopNextReadyIndexLocked();
                if (!next.has_value())
                {
                    return;
                }

                auto& task = m_Impl->Tasks[*next];
                launchIndex = *next;
                launchGeneration = task.Generation;
                launchCancellationGeneration = task.CancellationGeneration;
                task.Launched = true;
                task.State = StreamingTaskState::Running;
                executeFn = std::move(task.Desc.Execute);
                ++m_Impl->RunningCount;
            }

            auto runTask = [impl = m_Impl, index = launchIndex, generation = launchGeneration,
                            cancellationGeneration = launchCancellationGeneration,
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
                        .CancellationGeneration = cancellationGeneration,
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
                if (m_Impl->RunningCount > 0)
                {
                    --m_Impl->RunningCount;
                }
                continue;
            }

            auto& task = m_Impl->Tasks[completion.Index];
            if (m_Impl->RunningCount > 0)
            {
                --m_Impl->RunningCount;
            }

            if (task.Generation != completion.Generation)
            {
                continue;
            }

            task.Launched = false;

            if (task.CancellationGeneration != completion.CancellationGeneration)
            {
                if (task.Finalized)
                {
                    m_Impl->ReleaseReusableSlotLocked(completion.Index);
                }
                continue;
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

            if (task.Desc.ApplyOnMainThread)
            {
                if (IsReadbackRequest(*task.Result))
                {
                    task.State = StreamingTaskState::WaitingForReadback;
                }
                else
                {
                    task.State = IsUploadRequest(*task.Result)
                        ? StreamingTaskState::WaitingForGpuUpload
                        : StreamingTaskState::WaitingForMainThreadApply;
                    m_Impl->ReadyForApply.push_back(Impl::ReadyEntry{
                        .Index = completion.Index,
                        .Generation = task.Generation,
                    });
                }
            }
            else
            {
                m_Impl->FinalizeTaskLocked(completion.Index);
            }
        }
    }

    bool StreamingExecutor::ResumeReadback(const StreamingTaskHandle handle)
    {
        std::scoped_lock lock(m_Impl->Mutex);
        const auto index = m_Impl->Resolve(handle);
        if (!index.has_value())
        {
            return false;
        }

        auto& task = m_Impl->Tasks[*index];
        if (task.State != StreamingTaskState::WaitingForReadback ||
            !task.Result.has_value() ||
            !IsReadbackRequest(*task.Result))
        {
            return false;
        }

        task.State = StreamingTaskState::WaitingForMainThreadApply;
        m_Impl->ReadyForApply.push_back(Impl::ReadyEntry{
            .Index = *index,
            .Generation = task.Generation,
        });
        return true;
    }

    void StreamingExecutor::ApplyMainThreadResults()
    {
        (void)ApplyMainThreadResults(std::numeric_limits<std::uint32_t>::max());
    }

    std::uint32_t StreamingExecutor::ApplyMainThreadResults(
        const std::uint32_t maxApplyCount)
    {
        std::deque<Impl::ReadyEntry> ready{};
        {
            std::scoped_lock lock(m_Impl->Mutex);
            const auto count = std::min<std::size_t>(
                maxApplyCount,
                m_Impl->ReadyForApply.size());
            for (std::size_t i = 0u; i < count; ++i)
            {
                ready.push_back(m_Impl->ReadyForApply.front());
                m_Impl->ReadyForApply.pop_front();
            }
        }

        std::uint32_t applied = 0u;
        for (const auto entry : ready)
        {
            std::move_only_function<void(StreamingResult&&)> apply{};
            StreamingResult result = std::unexpected(Core::ErrorCode::Unknown);
            bool consumed = false;
            {
                std::scoped_lock lock(m_Impl->Mutex);
                if (entry.Index >= m_Impl->Tasks.size())
                {
                    continue;
                }

                auto& task = m_Impl->Tasks[entry.Index];
                if (task.Generation != entry.Generation ||
                    task.Finalized ||
                    task.Reusable ||
                    !task.Result.has_value())
                {
                    continue;
                }

                apply = std::move(task.Desc.ApplyOnMainThread);
                result = std::move(*task.Result);
                consumed = true;
            }

            if (apply)
            {
                apply(std::move(result));
            }

            {
                std::scoped_lock lock(m_Impl->Mutex);
                if (entry.Index < m_Impl->Tasks.size() &&
                    m_Impl->Tasks[entry.Index].Generation == entry.Generation)
                {
                    m_Impl->FinalizeTaskLocked(entry.Index);
                }
            }
            if (consumed)
            {
                ++applied;
            }
        }
        return applied;
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

            for (std::uint32_t index = 0u;
                 index < static_cast<std::uint32_t>(m_Impl->Tasks.size());
                 ++index)
            {
                auto& task = m_Impl->Tasks[index];
                if (task.State == StreamingTaskState::Pending)
                {
                    task.State = StreamingTaskState::Cancelled;
                    task.Result = CancelledResult();
                    m_Impl->FinalizeTaskLocked(index);
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

    std::vector<StreamingTaskState> StreamingExecutor::GetStates(
        const std::span<const StreamingTaskHandle> handles) const
    {
        std::vector<StreamingTaskState> states{};
        states.reserve(handles.size());

        std::scoped_lock lock(m_Impl->Mutex);
        for (const StreamingTaskHandle handle : handles)
        {
            const auto index = m_Impl->Resolve(handle);
            states.push_back(index.has_value()
                ? m_Impl->Tasks[*index].State
                : StreamingTaskState::Cancelled);
        }
        return states;
    }

    StreamingExecutorDiagnostics StreamingExecutor::GetDiagnostics() const
    {
        std::scoped_lock lock(m_Impl->Mutex);

        StreamingExecutorDiagnostics diagnostics{};
        diagnostics.SlotCount =
            static_cast<std::uint32_t>(m_Impl->Tasks.size());
        diagnostics.FreeSlotCount =
            static_cast<std::uint32_t>(m_Impl->FreeList.size());
        diagnostics.RunningCount = m_Impl->RunningCount;
        diagnostics.ReadyForApplyCount =
            static_cast<std::uint32_t>(m_Impl->ReadyForApply.size());

        for (const auto& task : m_Impl->Tasks)
        {
            if (!task.Reusable)
            {
                ++diagnostics.ActiveSlotCount;
            }
            if (task.QueuedReady)
            {
                ++diagnostics.ReadyTaskCount;
            }
        }

        return diagnostics;
    }
}
