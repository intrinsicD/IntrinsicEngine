// CORE-007 - scheduler priority and wait-registry smoke benchmark declaration.
//
// Measures deterministic synthetic scheduler work through public Core APIs.
// The priority probe is intentionally capable of emitting a schema-valid
// failed result so the pre-priority implementation remains a useful baseline.
#pragma once

#include <cstdint>

namespace Intrinsic::Bench::Core
{
    inline constexpr const char* kSchedulerHardeningSmokeBenchmarkId =
        "core.scheduler_hardening.smoke";
    inline constexpr const char* kSchedulerHardeningSmokeMethod =
        "core.scheduler_hardening";
    inline constexpr const char* kSchedulerHardeningSmokeDataset =
        "builtin.synthetic_scheduler_contention.v1";

    struct SchedulerHardeningSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double ThroughputItemsPerSecond{0.0};
        double QualityErrorL2{0.0};
        double PriorityProbeRuntimeMilliseconds{0.0};
        double WaitRegistrySingleThreadMedianMilliseconds{0.0};
        double WaitRegistryContendedMedianMilliseconds{0.0};
        double WaitRegistrySingleThreadThroughputItemsPerSecond{0.0};
        double WaitRegistryContendedThroughputItemsPerSecond{0.0};
        double WaitRegistryContendedScalingEfficiency{0.0};
        std::uint32_t WarmupIterations{0u};
        std::uint32_t MeasuredIterations{0u};
        std::uint32_t DispatchWorkerRequest{0u};
        std::uint32_t DispatchWorkerCount{0u};
        std::uint32_t DispatchTaskCount{0u};
        std::uint32_t PriorityLowTaskCount{0u};
        std::uint32_t PriorityHighTaskCount{0u};
        std::uint32_t PriorityLowBeforeFirstHigh{0u};
        std::uint32_t PriorityLowInHighWindow{0u};
        std::uint32_t WaitRegistryThreadCount{0u};
        std::uint32_t WaitRegistryOperationsPerThread{0u};
        bool DispatchSucceeded{false};
        bool PriorityContractSatisfied{false};
        bool WaitRegistrySucceeded{false};
        bool Succeeded{false};
    };

    [[nodiscard]] SchedulerHardeningSmokeMetrics RunSchedulerHardeningSmoke();
} // namespace Intrinsic::Bench::Core
