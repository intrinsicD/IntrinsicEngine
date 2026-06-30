module;

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

module Extrinsic.Graphics.ComputeParallelPrimitives;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
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

        [[nodiscard]] constexpr std::uint32_t CeilDiv(
            const std::uint32_t value,
            const std::uint32_t divisor) noexcept
        {
            return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
        }

        [[nodiscard]] constexpr std::uint64_t Uint32Bytes(
            const std::uint32_t count) noexcept
        {
            return static_cast<std::uint64_t>(count) * sizeof(std::uint32_t);
        }

        [[nodiscard]] std::vector<ParallelPrimitiveScratchLevel> BuildScanScratchLevels(
            const std::uint32_t elementCount,
            const std::uint32_t groupSize,
            const std::uint64_t baseOffsetBytes)
        {
            std::vector<ParallelPrimitiveScratchLevel> levels{};
            if (elementCount == 0u || groupSize == 0u)
            {
                return levels;
            }

            std::uint32_t levelElementCount = CeilDiv(elementCount, groupSize);
            std::uint64_t offsetBytes = baseOffsetBytes;
            std::uint32_t levelIndex = 0u;
            while (levelElementCount > 1u)
            {
                const std::uint32_t blockCount = CeilDiv(levelElementCount, groupSize);
                const std::uint64_t sizeBytes = Uint32Bytes(levelElementCount);
                levels.push_back(ParallelPrimitiveScratchLevel{
                    .LevelIndex = levelIndex,
                    .ElementCount = levelElementCount,
                    .BlockCount = blockCount,
                    .OffsetBytes = offsetBytes,
                    .SizeBytes = sizeBytes,
                });
                offsetBytes += sizeBytes;
                levelElementCount = blockCount;
                ++levelIndex;
            }
            return levels;
        }

        [[nodiscard]] std::uint64_t EndOfScratchLevels(
            const std::vector<ParallelPrimitiveScratchLevel>& levels,
            const std::uint64_t baseOffsetBytes) noexcept
        {
            if (levels.empty())
            {
                return baseOffsetBytes;
            }
            const ParallelPrimitiveScratchLevel& last = levels.back();
            return last.OffsetBytes + last.SizeBytes;
        }

        void AddScratchBarrier(ParallelPrimitiveDispatchPlan& plan,
                               const std::uint32_t afterDispatchIndex)
        {
            if (plan.ScratchBytes == 0u)
            {
                return;
            }
            plan.Barriers.push_back(ParallelPrimitiveBarrierDesc{
                .AfterDispatchIndex = afterDispatchIndex,
                .Buffer = ParallelPrimitiveBufferRole::Scratch,
                .Before = RHI::MemoryAccess::ShaderWrite,
                .After = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
            });
        }

        void AddShaderReadBarrier(ParallelPrimitiveDispatchPlan& plan,
                                  const std::uint32_t afterDispatchIndex,
                                  const ParallelPrimitiveBufferRole role)
        {
            plan.Barriers.push_back(ParallelPrimitiveBarrierDesc{
                .AfterDispatchIndex = afterDispatchIndex,
                .Buffer = role,
                .Before = RHI::MemoryAccess::ShaderWrite,
                .After = RHI::MemoryAccess::ShaderRead,
            });
        }

        void AppendPrefixScanDispatches(ParallelPrimitiveDispatchPlan& plan,
                                        const ParallelPrimitiveBufferRole inputRole,
                                        const ParallelPrimitiveBufferRole outputRole,
                                        const std::uint64_t inputOffsetBytes,
                                        const std::uint64_t outputOffsetBytes,
                                        const std::uint64_t levelBaseOffsetBytes)
        {
            if (plan.ElementCount == 0u)
            {
                return;
            }

            const bool hasScratchLevels = !plan.ScratchLevels.empty();
            const std::uint32_t firstDispatchIndex =
                static_cast<std::uint32_t>(plan.Dispatches.size());
            plan.Dispatches.push_back(ParallelPrimitiveDispatchDesc{
                .Kind = ParallelPrimitivePassKind::PrefixBlockScan,
                .Mode = plan.Mode,
                .LevelIndex = 0u,
                .ElementCount = plan.ElementCount,
                .GroupSize = plan.GroupSize,
                .GroupCountX = CeilDiv(plan.ElementCount, plan.GroupSize),
                .GroupCountY = 1u,
                .GroupCountZ = 1u,
                .InputRole = inputRole,
                .OutputRole = outputRole,
                .BlockSumsRole = hasScratchLevels
                    ? ParallelPrimitiveBufferRole::Scratch
                    : ParallelPrimitiveBufferRole::None,
                .InputOffsetBytes = inputOffsetBytes,
                .OutputOffsetBytes = outputOffsetBytes,
                .BlockSumsOffsetBytes = hasScratchLevels
                    ? plan.ScratchLevels.front().OffsetBytes
                    : kParallelPrimitiveInvalidOffset,
            });
            if (hasScratchLevels)
            {
                AddScratchBarrier(plan, firstDispatchIndex);
            }

            for (std::uint32_t level = 0u;
                 level < static_cast<std::uint32_t>(plan.ScratchLevels.size());
                 ++level)
            {
                const ParallelPrimitiveScratchLevel& scratch = plan.ScratchLevels[level];
                const bool hasNextLevel = level + 1u < plan.ScratchLevels.size();
                const std::uint32_t dispatchIndex =
                    static_cast<std::uint32_t>(plan.Dispatches.size());
                plan.Dispatches.push_back(ParallelPrimitiveDispatchDesc{
                    .Kind = ParallelPrimitivePassKind::PrefixBlockScan,
                    .Mode = PrefixScanMode::Exclusive,
                    .LevelIndex = level + 1u,
                    .ElementCount = scratch.ElementCount,
                    .GroupSize = plan.GroupSize,
                    .GroupCountX = CeilDiv(scratch.ElementCount, plan.GroupSize),
                    .GroupCountY = 1u,
                    .GroupCountZ = 1u,
                    .InputRole = ParallelPrimitiveBufferRole::Scratch,
                    .OutputRole = ParallelPrimitiveBufferRole::Scratch,
                    .BlockSumsRole = hasNextLevel
                        ? ParallelPrimitiveBufferRole::Scratch
                        : ParallelPrimitiveBufferRole::None,
                    .InputOffsetBytes = scratch.OffsetBytes,
                    .OutputOffsetBytes = scratch.OffsetBytes,
                    .BlockSumsOffsetBytes = hasNextLevel
                        ? plan.ScratchLevels[level + 1u].OffsetBytes
                        : kParallelPrimitiveInvalidOffset,
                });
                AddScratchBarrier(plan, dispatchIndex);
            }

            if (plan.ScratchLevels.size() > 1u)
            {
                for (std::uint32_t target =
                         static_cast<std::uint32_t>(plan.ScratchLevels.size() - 1u);
                     target > 0u;
                     --target)
                {
                    const std::uint32_t targetLevel = target - 1u;
                    const ParallelPrimitiveScratchLevel& scratch =
                        plan.ScratchLevels[targetLevel];
                    const ParallelPrimitiveScratchLevel& offsets =
                        plan.ScratchLevels[targetLevel + 1u];
                    const std::uint32_t dispatchIndex =
                        static_cast<std::uint32_t>(plan.Dispatches.size());
                    plan.Dispatches.push_back(ParallelPrimitiveDispatchDesc{
                        .Kind = ParallelPrimitivePassKind::PrefixAddBlockOffsets,
                        .Mode = PrefixScanMode::Exclusive,
                        .LevelIndex = targetLevel,
                        .ElementCount = scratch.ElementCount,
                        .GroupSize = plan.GroupSize,
                        .GroupCountX = CeilDiv(scratch.ElementCount, plan.GroupSize),
                        .GroupCountY = 1u,
                        .GroupCountZ = 1u,
                        .OutputRole = ParallelPrimitiveBufferRole::Scratch,
                        .OffsetsRole = ParallelPrimitiveBufferRole::Scratch,
                        .OutputOffsetBytes = scratch.OffsetBytes,
                        .OffsetsOffsetBytes = offsets.OffsetBytes,
                    });
                    AddScratchBarrier(plan, dispatchIndex);
                }
            }

            if (hasScratchLevels)
            {
                plan.Dispatches.push_back(ParallelPrimitiveDispatchDesc{
                    .Kind = ParallelPrimitivePassKind::PrefixAddBlockOffsets,
                    .Mode = PrefixScanMode::Exclusive,
                    .LevelIndex = 0u,
                    .ElementCount = plan.ElementCount,
                    .GroupSize = plan.GroupSize,
                    .GroupCountX = CeilDiv(plan.ElementCount, plan.GroupSize),
                    .GroupCountY = 1u,
                    .GroupCountZ = 1u,
                    .OutputRole = outputRole,
                    .OffsetsRole = ParallelPrimitiveBufferRole::Scratch,
                    .OutputOffsetBytes = outputOffsetBytes,
                    .OffsetsOffsetBytes = levelBaseOffsetBytes,
                });
            }
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

    const char* DebugNameForParallelPrimitivePassKind(
        const ParallelPrimitivePassKind kind) noexcept
    {
        switch (kind)
        {
        case ParallelPrimitivePassKind::PrefixBlockScan:
            return "PrefixBlockScan";
        case ParallelPrimitivePassKind::PrefixAddBlockOffsets:
            return "PrefixAddBlockOffsets";
        case ParallelPrimitivePassKind::StreamCompactScatter:
            return "StreamCompactScatter";
        }
        return "Unknown";
    }

    const char* DebugNameForParallelPrimitiveBufferRole(
        const ParallelPrimitiveBufferRole role) noexcept
    {
        switch (role)
        {
        case ParallelPrimitiveBufferRole::None:
            return "None";
        case ParallelPrimitiveBufferRole::Input:
            return "Input";
        case ParallelPrimitiveBufferRole::Output:
            return "Output";
        case ParallelPrimitiveBufferRole::Keys:
            return "Keys";
        case ParallelPrimitiveBufferRole::Flags:
            return "Flags";
        case ParallelPrimitiveBufferRole::OutputKeys:
            return "OutputKeys";
        case ParallelPrimitiveBufferRole::OutputCount:
            return "OutputCount";
        case ParallelPrimitiveBufferRole::Scratch:
            return "Scratch";
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

    ParallelPrimitiveDispatchPlan ComputePrefixScanDispatchPlan(
        const std::uint32_t elementCount,
        const PrefixScanMode mode,
        const std::uint32_t groupSize)
    {
        ParallelPrimitiveDispatchPlan plan{};
        plan.Kind = ParallelPrimitiveKind::PrefixScan;
        plan.Mode = mode;
        plan.ElementCount = elementCount;
        plan.GroupSize = groupSize;

        if (groupSize == 0u)
        {
            plan.Status = ParallelPrimitiveStatus::InvalidInput;
            return plan;
        }

        if (elementCount == 0u)
        {
            return plan;
        }

        plan.ScratchLevels = BuildScanScratchLevels(elementCount, groupSize, 0u);
        plan.ScratchBytes = EndOfScratchLevels(plan.ScratchLevels, 0u);

        AppendPrefixScanDispatches(plan,
                                   ParallelPrimitiveBufferRole::Input,
                                   ParallelPrimitiveBufferRole::Output,
                                   kParallelPrimitiveInvalidOffset,
                                   kParallelPrimitiveInvalidOffset,
                                   plan.ScratchLevels.empty()
                                       ? kParallelPrimitiveInvalidOffset
                                       : plan.ScratchLevels.front().OffsetBytes);

        if (!plan.Dispatches.empty())
        {
            AddShaderReadBarrier(plan,
                                 static_cast<std::uint32_t>(plan.Dispatches.size() - 1u),
                                 ParallelPrimitiveBufferRole::Output);
        }
        return plan;
    }

    ParallelPrimitiveDispatchPlan ComputeStreamCompactionDispatchPlan(
        const std::uint32_t elementCount,
        const std::uint32_t groupSize)
    {
        ParallelPrimitiveDispatchPlan plan{};
        plan.Kind = ParallelPrimitiveKind::StreamCompaction;
        plan.Mode = PrefixScanMode::Exclusive;
        plan.ElementCount = elementCount;
        plan.GroupSize = groupSize;

        if (groupSize == 0u)
        {
            plan.Status = ParallelPrimitiveStatus::InvalidInput;
            return plan;
        }

        if (elementCount == 0u)
        {
            return plan;
        }

        plan.PrefixOffsetsOffsetBytes = 0u;
        plan.PrefixOffsetsSizeBytes = Uint32Bytes(elementCount);
        plan.ScratchLevels = BuildScanScratchLevels(elementCount,
                                                    groupSize,
                                                    plan.PrefixOffsetsSizeBytes);
        plan.ScratchBytes = EndOfScratchLevels(plan.ScratchLevels,
                                               plan.PrefixOffsetsSizeBytes);

        AppendPrefixScanDispatches(plan,
                                   ParallelPrimitiveBufferRole::Flags,
                                   ParallelPrimitiveBufferRole::Scratch,
                                   kParallelPrimitiveInvalidOffset,
                                   plan.PrefixOffsetsOffsetBytes,
                                   plan.ScratchLevels.empty()
                                       ? kParallelPrimitiveInvalidOffset
                                       : plan.ScratchLevels.front().OffsetBytes);

        if (!plan.Dispatches.empty())
        {
            AddScratchBarrier(plan,
                              static_cast<std::uint32_t>(plan.Dispatches.size() - 1u));
        }

        const std::uint32_t scatterDispatchIndex =
            static_cast<std::uint32_t>(plan.Dispatches.size());
        plan.Dispatches.push_back(ParallelPrimitiveDispatchDesc{
            .Kind = ParallelPrimitivePassKind::StreamCompactScatter,
            .Mode = PrefixScanMode::Exclusive,
            .LevelIndex = 0u,
            .ElementCount = elementCount,
            .GroupSize = groupSize,
            .GroupCountX = CeilDiv(elementCount, groupSize),
            .GroupCountY = 1u,
            .GroupCountZ = 1u,
            .InputRole = ParallelPrimitiveBufferRole::Keys,
            .OutputRole = ParallelPrimitiveBufferRole::OutputKeys,
            .OffsetsRole = ParallelPrimitiveBufferRole::Scratch,
            .CountRole = ParallelPrimitiveBufferRole::OutputCount,
            .OffsetsOffsetBytes = plan.PrefixOffsetsOffsetBytes,
        });
        AddShaderReadBarrier(plan, scatterDispatchIndex,
                             ParallelPrimitiveBufferRole::OutputKeys);
        AddShaderReadBarrier(plan, scatterDispatchIndex,
                             ParallelPrimitiveBufferRole::OutputCount);
        return plan;
    }

    RHI::BufferDesc BuildParallelPrimitiveScratchBufferDesc(
        const ParallelPrimitiveDispatchPlan& plan,
        const char* debugName) noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = plan.IsValid() ? plan.ScratchBytes : 0u,
            .Usage = RHI::BufferUsage::Storage |
                     RHI::BufferUsage::TransferSrc |
                     RHI::BufferUsage::TransferDst,
            .HostVisible = false,
            .DebugName = debugName,
        };
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
