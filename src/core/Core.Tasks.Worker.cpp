module;

#include <atomic>
#include <chrono>
#include <cstdint>

module Extrinsic.Core.Tasks;

import :Internal;

namespace Extrinsic::Core::Tasks
{
    void Scheduler::WorkerEntry(unsigned threadIndex)
    {
        static constexpr uint32_t FairnessInterval = 32;
        s_WorkerIndex = static_cast<int>(threadIndex);
        uint32_t lastSignal =
            s_Ctx->workSignal.load(std::memory_order_seq_cst);
        uint32_t localPopBudget = FairnessInterval;

        while (s_Ctx->isRunning.load(std::memory_order_acquire))
        {
            LocalTask task;
            const bool shouldForceFairness = localPopBudget == 0;
            const bool canUseLocal = !shouldForceFairness;
            bool poppedLocal = false;
            bool usedFairnessFallback = false;
            std::uint8_t lane =
                static_cast<std::uint8_t>(DispatchPriority::Normal);
            bool hasTask = TryPopWorkerTask(
                task, threadIndex, lane, canUseLocal, poppedLocal);

            if (!hasTask && shouldForceFairness)
            {
                // A fairness probe must not park while this worker still owns
                // local work. Check the external/victim paths once, then
                // admit local work again before publishing a parked state.
                hasTask = TryPopWorkerTask(
                    task, threadIndex, lane, true, poppedLocal);
                usedFairnessFallback = hasTask && poppedLocal;
            }

            if (hasTask)
            {
                if (usedFairnessFallback || !poppedLocal)
                    localPopBudget = FairnessInterval;
                else if (localPopBudget > 0u)
                    --localPopBudget;

                OnTaskDequeuedAndRun(task, lane);
            }
            else
            {
                const auto idleWaitStart = std::chrono::steady_clock::now();
                // In the single seq_cst order, either dispatch advances the
                // signal first and this recheck avoids sleeping, or this park
                // publication comes first and dispatch observes a parked
                // worker and notifies it.
                s_Ctx->parkedWorkerCount.fetch_add(
                    1u, std::memory_order_seq_cst);
                const auto currentSignal =
                    s_Ctx->workSignal.load(std::memory_order_seq_cst);
                bool waited = false;
                if (currentSignal == lastSignal &&
                    s_Ctx->isRunning.load(std::memory_order_acquire))
                {
                    waited = true;
                    s_Ctx->workSignal.wait(
                        lastSignal, std::memory_order_acquire);
                }
                s_Ctx->parkedWorkerCount.fetch_sub(
                    1u, std::memory_order_seq_cst);

                if (waited)
                {
                    const auto idleWaitNs =
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - idleWaitStart)
                            .count();
                    s_Ctx->idleWaitCount.fetch_add(
                        1, std::memory_order_relaxed);
                    s_Ctx->idleWaitTotalNs.fetch_add(
                        static_cast<uint64_t>(idleWaitNs),
                        std::memory_order_relaxed);
                }

                lastSignal =
                    s_Ctx->workSignal.load(std::memory_order_seq_cst);
                localPopBudget = FairnessInterval;
            }
        }

        s_WorkerIndex = -1;
    }
}
