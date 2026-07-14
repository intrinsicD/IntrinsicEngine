module;

#include <memory>
#include <thread>
#include <optional>
#include <iterator>
#include <mutex>
#include <vector>

module Extrinsic.Core.Tasks;

import :Internal;
import Extrinsic.Core.Logging;

namespace Extrinsic::Core::Tasks
{
    void Scheduler::Initialize(unsigned threadCount)
    {
        if (s_Ctx) return;
        s_Ctx = std::make_unique<Detail::SchedulerContext>();
        s_Ctx->isRunning = true;

        if (threadCount == 0)
            threadCount = std::thread::hardware_concurrency();
        if (threadCount > 2) threadCount--;

        threadCount = std::max(threadCount, 1u);

        s_Ctx->workerStates.resize(threadCount);

        for (unsigned i = 0; i < threadCount; ++i)
            s_Ctx->workers.emplace_back([i] { WorkerEntry(i); });

        Log::Info("Initialized Scheduler with {} worker threads.", threadCount);
    }

    void Scheduler::Shutdown()
    {
        if (!s_Ctx) return;

        s_Ctx->isRunning = false;
        s_Ctx->workSignal.fetch_add(1, std::memory_order_release);
        s_Ctx->workSignal.notify_all();

        for (auto& t : s_Ctx->workers)
        {
            if (t.joinable())
                t.join();
        }

        std::vector<Detail::SchedulerContext::ParkedContinuation> abandoned;
        for (auto& shard : s_Ctx->waitShards)
        {
            std::lock_guard lock(shard.Mutex);
            for (auto& slot : shard.Slots)
            {
                auto slotContinuations =
                    Detail::TakeParkedContinuationsLocked(shard, slot);
                abandoned.insert(abandoned.end(),
                                 std::make_move_iterator(slotContinuations.begin()),
                                 std::make_move_iterator(slotContinuations.end()));
            }
        }
        Detail::DestroyParkedContinuations(abandoned);
        s_Ctx.reset();
    }

    bool Scheduler::IsInitialized() noexcept
    {
        return s_Ctx != nullptr;
    }

    void Scheduler::WaitForAll()
    {
        if (!s_Ctx)
            return;

        for (;;)
        {
            if (s_Ctx->inFlightTasks.load(std::memory_order_acquire) == 0)
                break;

            LocalTask task;
            std::uint8_t lane = static_cast<std::uint8_t>(DispatchPriority::Normal);
            if (TryPopTask(task, std::nullopt, &lane))
            {
                OnTaskDequeuedAndRun(task, lane);
                continue;
            }

            const auto observed = s_Ctx->inFlightTasks.load(std::memory_order_acquire);
            if (observed > 0)
                s_Ctx->inFlightTasks.wait(observed, std::memory_order_relaxed);
            else
                Detail::CpuRelaxOrYield();
        }
    }
}
