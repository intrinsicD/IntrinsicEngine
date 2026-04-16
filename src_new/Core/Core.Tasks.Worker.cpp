module;

#include <atomic>
#include <chrono>

module Extrinsic.Core.Tasks;

import :Internal;

namespace Extrinsic::Core::Tasks
{
    void Scheduler::WorkerEntry(unsigned threadIndex)
    {
        static constexpr uint32_t FairnessInterval = 32;
        s_WorkerIndex = static_cast<int>(threadIndex);
        uint32_t lastSignal = s_Ctx->workSignal.load(std::memory_order_acquire);
        uint32_t localPopBudget = FairnessInterval;

        while (s_Ctx->isRunning)
        {
            LocalTask task;
            const bool shouldForceFairness = localPopBudget == 0;
            const bool canUseLocal = !shouldForceFairness;
            bool hasTask = false;

            if (canUseLocal && TryPopLocal(threadIndex, task))
            {
                hasTask = true;
                if (localPopBudget > 0)
                    localPopBudget -= 1;
            }
            else
            {
                if (TrySteal(threadIndex, task) || TryPopGlobalInject(task))
                {
                    hasTask = true;
                    localPopBudget = FairnessInterval;
                }
            }

            if (hasTask)
            {
                OnTaskDequeuedAndRun(task);
            }
            else
            {
                const auto idleWaitStart = std::chrono::steady_clock::now();
                s_Ctx->workSignal.wait(lastSignal);
                const auto idleWaitNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - idleWaitStart).count();
                s_Ctx->idleWaitCount.fetch_add(1, std::memory_order_relaxed);
                s_Ctx->idleWaitTotalNs.fetch_add(static_cast<uint64_t>(idleWaitNs), std::memory_order_relaxed);
                lastSignal = s_Ctx->workSignal.load(std::memory_order_acquire);
                localPopBudget = FairnessInterval;
            }
        }

        s_WorkerIndex = -1;
    }
}
