module;

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif
#include <thread>
#include <cmath>
#include <algorithm>

module Extrinsic.Core.Tasks:Internal.Impl;
import :Internal;

namespace Extrinsic::Core::Tasks
{
    std::unique_ptr<Detail::SchedulerContext> s_Ctx{};
    thread_local int s_WorkerIndex = -1;

    namespace Detail
    {
        bool SpinLock::try_lock()
        {
            return !locked.exchange(true, std::memory_order_acquire);
        }

        void SpinLock::lock()
        {
            while (true)
            {
                if (!locked.exchange(true, std::memory_order_acquire))
                    return;

                constexpr int kSpinIters = 64;
                int spins = 0;
                while (locked.load(std::memory_order_relaxed))
                {
                    if (spins++ < kSpinIters)
                    {
                        Detail::CpuRelaxOrYield();
                        continue;
                    }

                    locked.wait(true, std::memory_order_relaxed);
                    spins = 0;
                }
            }
        }

        void SpinLock::unlock()
        {
            locked.store(false, std::memory_order_release);
            locked.notify_one();
        }

        void CpuRelaxOrYield() noexcept
        {
            if (!CpuRelaxOnce())
                std::this_thread::yield();
        }

        [[nodiscard]] bool CpuRelaxOnce() noexcept
        {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
            _mm_pause();
            return true;
#elif defined(__aarch64__) || defined(__arm__)
            asm volatile("yield" ::: "memory");
            return true;
#else
            return false;
#endif
        }

        SchedulerContext::WorkerState::WorkerState(WorkerState&& other) noexcept
            : localDeque(std::move(other.localDeque))
              , stealCount(other.stealCount.load(std::memory_order_relaxed))
        {
            other.stealCount.store(0, std::memory_order_relaxed);
        }

        SchedulerContext::WorkerState& SchedulerContext::WorkerState::operator=(WorkerState&& other) noexcept
        {
            if (this != &other)
            {
                localDeque = std::move(other.localDeque);
                stealCount.store(other.stealCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
                other.stealCount.store(0, std::memory_order_relaxed);
            }
            return *this;
        }

        uint64_t EstimateLatencyPercentile(
            const std::array<uint64_t, SchedulerContext::LatencyBucketCount>& histogram,
            const uint64_t totalSamples,
            const double percentile)
        {
            if (totalSamples == 0)
                return 0;

            const auto targetRank = static_cast<uint64_t>(
                std::ceil(std::clamp(percentile, 0.0, 1.0) * static_cast<double>(totalSamples)));
            uint64_t prefix = 0;
            for (size_t i = 0; i < histogram.size(); ++i)
            {
                prefix += histogram[i];
                if (prefix >= targetRank)
                    return (i == 0) ? 0 : (1ull << (i - 1));
            }

            return 1ull << (histogram.size() - 2);
        }

        void RecordLatencySample(
            std::array<std::atomic<uint64_t>, SchedulerContext::LatencyBucketCount>& histogram,
            const uint64_t latencyNs)
        {
            histogram[LatencyBucketIndex(latencyNs)].fetch_add(1, std::memory_order_relaxed);
        }

        size_t LatencyBucketIndex(const uint64_t latencyNs)
        {
            if (latencyNs == 0)
                return 0;

            const uint32_t msb = std::bit_width(latencyNs) - 1;
            return std::min<size_t>(msb + 1, SchedulerContext::LatencyBucketCount - 1);
        }
    }
}
