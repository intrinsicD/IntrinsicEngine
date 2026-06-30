module;

#include <cstdint>
#include <limits>
#include <span>

module Extrinsic.Graphics.ComputeParallelPrimitives;

import Extrinsic.RHI.Device;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] bool AddWouldOverflow(
            const std::uint64_t accumulator,
            const std::uint32_t value) noexcept
        {
            return accumulator + static_cast<std::uint64_t>(value) >
                   static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
        }

        [[nodiscard]] GpuParallelPrimitiveRecordResult DeviceUnavailableResult(
            const ParallelPrimitiveKind kind,
            const std::uint32_t elementCount) noexcept
        {
            return GpuParallelPrimitiveRecordResult{
                .Status = ParallelPrimitiveStatus::DeviceUnavailable,
                .Kind = kind,
                .Diagnostics = ParallelPrimitiveDiagnostics{.ElementCount = elementCount},
                .Recorded = false,
                .CpuFallbackRecommended = true,
            };
        }

        [[nodiscard]] GpuParallelPrimitiveRecordResult UnsupportedGpuSliceResult(
            const ParallelPrimitiveKind kind,
            const std::uint32_t elementCount) noexcept
        {
            return GpuParallelPrimitiveRecordResult{
                .Status = ParallelPrimitiveStatus::UnsupportedInCurrentSlice,
                .Kind = kind,
                .Diagnostics = ParallelPrimitiveDiagnostics{.ElementCount = elementCount},
                .Recorded = false,
                .CpuFallbackRecommended = true,
            };
        }
    }

    const char* DebugNameForParallelPrimitiveStatus(
        const ParallelPrimitiveStatus status) noexcept
    {
        switch (status)
        {
        case ParallelPrimitiveStatus::Success:
            return "Success";
        case ParallelPrimitiveStatus::InvalidInput:
            return "InvalidInput";
        case ParallelPrimitiveStatus::SizeMismatch:
            return "SizeMismatch";
        case ParallelPrimitiveStatus::OutputTooSmall:
            return "OutputTooSmall";
        case ParallelPrimitiveStatus::SumOverflow:
            return "SumOverflow";
        case ParallelPrimitiveStatus::DeviceUnavailable:
            return "DeviceUnavailable";
        case ParallelPrimitiveStatus::InvalidGpuResource:
            return "InvalidGpuResource";
        case ParallelPrimitiveStatus::UnsupportedInCurrentSlice:
            return "UnsupportedInCurrentSlice";
        }
        return "Unknown";
    }

    ParallelPrimitiveCpuResult ComputePrefixScanCpu(
        const std::span<const std::uint32_t> values,
        const std::span<std::uint32_t> output,
        const PrefixScanMode mode) noexcept
    {
        ParallelPrimitiveCpuResult result{};
        result.Diagnostics.ElementCount = static_cast<std::uint32_t>(values.size());
        result.Diagnostics.OutputCount = static_cast<std::uint32_t>(values.size());

        if (output.size() < values.size())
        {
            result.Status = ParallelPrimitiveStatus::OutputTooSmall;
            result.Diagnostics.OutputCount = static_cast<std::uint32_t>(output.size());
            return result;
        }

        std::uint64_t accumulator = 0u;
        for (std::size_t index = 0u; index < values.size(); ++index)
        {
            const std::uint32_t value = values[index];
            if (AddWouldOverflow(accumulator, value))
            {
                result.Status = ParallelPrimitiveStatus::SumOverflow;
                result.Diagnostics.FirstFailureIndex = static_cast<std::uint32_t>(index);
                result.Diagnostics.Total = accumulator + static_cast<std::uint64_t>(value);
                return result;
            }

            if (mode == PrefixScanMode::Exclusive)
            {
                output[index] = static_cast<std::uint32_t>(accumulator);
                accumulator += static_cast<std::uint64_t>(value);
            }
            else
            {
                accumulator += static_cast<std::uint64_t>(value);
                output[index] = static_cast<std::uint32_t>(accumulator);
            }
        }

        result.Diagnostics.Total = accumulator;
        return result;
    }

    ParallelPrimitiveCpuResult CompactByFlagsCpu(
        const std::span<const std::uint32_t> keys,
        const std::span<const std::uint32_t> flags,
        const std::span<std::uint32_t> outputKeys) noexcept
    {
        ParallelPrimitiveCpuResult result{};
        result.Diagnostics.ElementCount = static_cast<std::uint32_t>(keys.size());

        if (keys.size() != flags.size())
        {
            result.Status = ParallelPrimitiveStatus::SizeMismatch;
            result.Diagnostics.OutputCount = static_cast<std::uint32_t>(outputKeys.size());
            return result;
        }

        std::uint32_t keptCount = 0u;
        for (const std::uint32_t flag : flags)
        {
            if (flag != 0u)
            {
                ++keptCount;
            }
        }

        result.Diagnostics.OutputCount = keptCount;
        result.Diagnostics.Total = keptCount;
        if (outputKeys.size() < keptCount)
        {
            result.Status = ParallelPrimitiveStatus::OutputTooSmall;
            return result;
        }

        std::uint32_t writeIndex = 0u;
        for (std::size_t index = 0u; index < keys.size(); ++index)
        {
            if (flags[index] != 0u)
            {
                outputKeys[writeIndex] = keys[index];
                ++writeIndex;
            }
        }

        return result;
    }

    GpuParallelPrimitiveRecordResult RecordGpuPrefixScan(
        const GpuPrefixScanRecordDesc& desc) noexcept
    {
        if (desc.Device == nullptr)
        {
            return GpuParallelPrimitiveRecordResult{
                .Status = ParallelPrimitiveStatus::InvalidInput,
                .Kind = ParallelPrimitiveKind::PrefixScan,
                .Diagnostics = ParallelPrimitiveDiagnostics{.ElementCount = desc.ElementCount},
            };
        }

        if (!desc.Device->IsOperational())
        {
            return DeviceUnavailableResult(ParallelPrimitiveKind::PrefixScan,
                                           desc.ElementCount);
        }

        if (desc.ElementCount > 0u &&
            (!desc.Input.IsValid() || !desc.Output.IsValid() ||
             !desc.Scratch.IsValid()))
        {
            return GpuParallelPrimitiveRecordResult{
                .Status = ParallelPrimitiveStatus::InvalidGpuResource,
                .Kind = ParallelPrimitiveKind::PrefixScan,
                .Diagnostics = ParallelPrimitiveDiagnostics{.ElementCount = desc.ElementCount},
            };
        }

        return UnsupportedGpuSliceResult(ParallelPrimitiveKind::PrefixScan,
                                         desc.ElementCount);
    }

    GpuParallelPrimitiveRecordResult RecordGpuStreamCompaction(
        const GpuStreamCompactionRecordDesc& desc) noexcept
    {
        if (desc.Device == nullptr)
        {
            return GpuParallelPrimitiveRecordResult{
                .Status = ParallelPrimitiveStatus::InvalidInput,
                .Kind = ParallelPrimitiveKind::StreamCompaction,
                .Diagnostics = ParallelPrimitiveDiagnostics{.ElementCount = desc.ElementCount},
            };
        }

        if (!desc.Device->IsOperational())
        {
            return DeviceUnavailableResult(ParallelPrimitiveKind::StreamCompaction,
                                           desc.ElementCount);
        }

        if (desc.ElementCount > 0u &&
            (!desc.Keys.IsValid() || !desc.Flags.IsValid() ||
             !desc.OutputKeys.IsValid() || !desc.OutputCount.IsValid() ||
             !desc.Scratch.IsValid()))
        {
            return GpuParallelPrimitiveRecordResult{
                .Status = ParallelPrimitiveStatus::InvalidGpuResource,
                .Kind = ParallelPrimitiveKind::StreamCompaction,
                .Diagnostics = ParallelPrimitiveDiagnostics{.ElementCount = desc.ElementCount},
            };
        }

        return UnsupportedGpuSliceResult(ParallelPrimitiveKind::StreamCompaction,
                                         desc.ElementCount);
    }
}
