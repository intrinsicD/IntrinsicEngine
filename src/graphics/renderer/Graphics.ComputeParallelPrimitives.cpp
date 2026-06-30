module;

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Graphics.ComputeParallelPrimitives;

import Extrinsic.RHI.BufferManager;
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

        [[nodiscard]] GpuParallelPrimitiveRecordResult StatusResult(
            const ParallelPrimitiveStatus status,
            const ParallelPrimitiveKind kind,
            const std::uint32_t elementCount,
            const bool cpuFallbackRecommended = false) noexcept
        {
            return GpuParallelPrimitiveRecordResult{
                .Status = status,
                .Kind = kind,
                .Diagnostics = ParallelPrimitiveDiagnostics{.ElementCount = elementCount},
                .Recorded = false,
                .CpuFallbackRecommended = cpuFallbackRecommended,
            };
        }

        [[nodiscard]] GpuParallelPrimitiveRecordResult DeviceUnavailableResult(
            const ParallelPrimitiveKind kind,
            const std::uint32_t elementCount) noexcept
        {
            return StatusResult(ParallelPrimitiveStatus::DeviceUnavailable,
                                kind,
                                elementCount,
                                true);
        }

        struct RoleBindings
        {
            RHI::BufferHandle Input{};
            RHI::BufferHandle Output{};
            RHI::BufferHandle Keys{};
            RHI::BufferHandle Flags{};
            RHI::BufferHandle OutputKeys{};
            RHI::BufferHandle OutputCount{};
            RHI::BufferHandle Scratch{};
            std::uint64_t InputBDA = 0u;
            std::uint64_t OutputBDA = 0u;
            std::uint64_t KeysBDA = 0u;
            std::uint64_t FlagsBDA = 0u;
            std::uint64_t OutputKeysBDA = 0u;
            std::uint64_t OutputCountBDA = 0u;
            std::uint64_t ScratchBDA = 0u;
        };

        [[nodiscard]] RHI::BufferHandle HandleForRole(
            const RoleBindings& bindings,
            const ParallelPrimitiveBufferRole role) noexcept
        {
            switch (role)
            {
            case ParallelPrimitiveBufferRole::None:
                return {};
            case ParallelPrimitiveBufferRole::Input:
                return bindings.Input;
            case ParallelPrimitiveBufferRole::Output:
                return bindings.Output;
            case ParallelPrimitiveBufferRole::Keys:
                return bindings.Keys;
            case ParallelPrimitiveBufferRole::Flags:
                return bindings.Flags;
            case ParallelPrimitiveBufferRole::OutputKeys:
                return bindings.OutputKeys;
            case ParallelPrimitiveBufferRole::OutputCount:
                return bindings.OutputCount;
            case ParallelPrimitiveBufferRole::Scratch:
                return bindings.Scratch;
            }
            return {};
        }

        [[nodiscard]] std::uint64_t AddressForRole(
            const RoleBindings& bindings,
            const ParallelPrimitiveBufferRole role,
            const std::uint64_t offsetBytes) noexcept
        {
            std::uint64_t base = 0u;
            switch (role)
            {
            case ParallelPrimitiveBufferRole::None:
                return 0u;
            case ParallelPrimitiveBufferRole::Input:
                base = bindings.InputBDA;
                break;
            case ParallelPrimitiveBufferRole::Output:
                base = bindings.OutputBDA;
                break;
            case ParallelPrimitiveBufferRole::Keys:
                base = bindings.KeysBDA;
                break;
            case ParallelPrimitiveBufferRole::Flags:
                base = bindings.FlagsBDA;
                break;
            case ParallelPrimitiveBufferRole::OutputKeys:
                base = bindings.OutputKeysBDA;
                break;
            case ParallelPrimitiveBufferRole::OutputCount:
                base = bindings.OutputCountBDA;
                break;
            case ParallelPrimitiveBufferRole::Scratch:
                base = bindings.ScratchBDA;
                break;
            }

            if (base == 0u)
            {
                return 0u;
            }
            return offsetBytes == kParallelPrimitiveInvalidOffset
                ? base
                : base + offsetBytes;
        }

        [[nodiscard]] bool DispatchResourcesAreValid(
            const RoleBindings& bindings,
            const ParallelPrimitiveDispatchDesc& dispatch) noexcept
        {
            const auto validAddress =
                [&bindings](const ParallelPrimitiveBufferRole role,
                            const std::uint64_t offsetBytes) noexcept
                {
                    return role == ParallelPrimitiveBufferRole::None ||
                           AddressForRole(bindings, role, offsetBytes) != 0u;
                };

            return validAddress(dispatch.InputRole, dispatch.InputOffsetBytes) &&
                   validAddress(dispatch.OutputRole, dispatch.OutputOffsetBytes) &&
                   validAddress(dispatch.BlockSumsRole, dispatch.BlockSumsOffsetBytes) &&
                   validAddress(dispatch.OffsetsRole, dispatch.OffsetsOffsetBytes) &&
                   validAddress(dispatch.CountRole, dispatch.CountOffsetBytes);
        }

        [[nodiscard]] RHI::PipelineHandle PipelineForDispatch(
            const ParallelPrimitivePipelineSet& pipelines,
            const ParallelPrimitivePassKind kind) noexcept
        {
            switch (kind)
            {
            case ParallelPrimitivePassKind::PrefixBlockScan:
                return pipelines.PrefixScan;
            case ParallelPrimitivePassKind::PrefixAddBlockOffsets:
                return pipelines.AddBlockOffsets;
            case ParallelPrimitivePassKind::StreamCompactScatter:
                return pipelines.CompactByFlags;
            }
            return {};
        }

        [[nodiscard]] bool PlanResourcesAreRecordable(
            const ParallelPrimitiveDispatchPlan& plan,
            const RoleBindings& bindings,
            const ParallelPrimitivePipelineSet& pipelines) noexcept
        {
            for (const ParallelPrimitiveDispatchDesc& dispatch : plan.Dispatches)
            {
                if (!PipelineForDispatch(pipelines, dispatch.Kind).IsValid() ||
                    !DispatchResourcesAreValid(bindings, dispatch))
                {
                    return false;
                }
                if (dispatch.Kind == ParallelPrimitivePassKind::StreamCompactScatter &&
                    bindings.FlagsBDA == 0u)
                {
                    return false;
                }
            }

            for (const ParallelPrimitiveBarrierDesc& barrier : plan.Barriers)
            {
                if (!HandleForRole(bindings, barrier.Buffer).IsValid())
                {
                    return false;
                }
            }
            return true;
        }

        void RecordPlanDispatches(RHI::ICommandContext& cmd,
                                  const ParallelPrimitiveDispatchPlan& plan,
                                  const RoleBindings& bindings,
                                  const ParallelPrimitivePipelineSet& pipelines)
        {
            for (std::uint32_t index = 0u;
                 index < static_cast<std::uint32_t>(plan.Dispatches.size());
                 ++index)
            {
                const ParallelPrimitiveDispatchDesc& dispatch = plan.Dispatches[index];
                cmd.BindPipeline(PipelineForDispatch(pipelines, dispatch.Kind));

                switch (dispatch.Kind)
                {
                case ParallelPrimitivePassKind::PrefixBlockScan:
                {
                    const ParallelPrefixScanPushConstants pc{
                        .InputBDA = AddressForRole(bindings,
                                                   dispatch.InputRole,
                                                   dispatch.InputOffsetBytes),
                        .OutputBDA = AddressForRole(bindings,
                                                    dispatch.OutputRole,
                                                    dispatch.OutputOffsetBytes),
                        .BlockSumsBDA = AddressForRole(bindings,
                                                       dispatch.BlockSumsRole,
                                                       dispatch.BlockSumsOffsetBytes),
                        .ElementCount = dispatch.ElementCount,
                        .Mode = (dispatch.Mode == PrefixScanMode::Inclusive
                                     ? kParallelPrefixScanModeInclusiveBit
                                     : 0u) |
                                (dispatch.InputRole == ParallelPrimitiveBufferRole::Flags
                                     ? kParallelPrefixScanModeNormalizeInputBit
                                     : 0u),
                        .LevelIndex = dispatch.LevelIndex,
                        .Reserved0 = 0u,
                    };
                    cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)), 0u);
                    break;
                }
                case ParallelPrimitivePassKind::PrefixAddBlockOffsets:
                {
                    const ParallelScanAddOffsetsPushConstants pc{
                        .OutputBDA = AddressForRole(bindings,
                                                    dispatch.OutputRole,
                                                    dispatch.OutputOffsetBytes),
                        .OffsetsBDA = AddressForRole(bindings,
                                                     dispatch.OffsetsRole,
                                                     dispatch.OffsetsOffsetBytes),
                        .ElementCount = dispatch.ElementCount,
                        .Reserved0 = 0u,
                        .Reserved1 = 0u,
                        .Reserved2 = 0u,
                    };
                    cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)), 0u);
                    break;
                }
                case ParallelPrimitivePassKind::StreamCompactScatter:
                {
                    const ParallelCompactByFlagsPushConstants pc{
                        .KeysBDA = AddressForRole(bindings,
                                                  dispatch.InputRole,
                                                  dispatch.InputOffsetBytes),
                        .FlagsBDA = bindings.FlagsBDA,
                        .OffsetsBDA = AddressForRole(bindings,
                                                     dispatch.OffsetsRole,
                                                     dispatch.OffsetsOffsetBytes),
                        .OutputKeysBDA = AddressForRole(bindings,
                                                        dispatch.OutputRole,
                                                        dispatch.OutputOffsetBytes),
                        .OutputCountBDA = AddressForRole(bindings,
                                                         dispatch.CountRole,
                                                         dispatch.CountOffsetBytes),
                        .ElementCount = dispatch.ElementCount,
                        .Reserved0 = 0u,
                        .Reserved1 = 0u,
                        .Reserved2 = 0u,
                    };
                    cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)), 0u);
                    break;
                }
                }

                cmd.Dispatch(dispatch.GroupCountX,
                             dispatch.GroupCountY,
                             dispatch.GroupCountZ);

                for (const ParallelPrimitiveBarrierDesc& barrier : plan.Barriers)
                {
                    if (barrier.AfterDispatchIndex == index)
                    {
                        cmd.BufferBarrier(HandleForRole(bindings, barrier.Buffer),
                                          barrier.Before,
                                          barrier.After);
                    }
                }
            }
        }

        [[nodiscard]] bool ScratchBufferIsLargeEnough(
            const RHI::BufferManager* buffers,
            const RHI::BufferHandle scratch,
            const std::uint64_t requiredBytes) noexcept
        {
            if (requiredBytes == 0u || !scratch.IsValid() || buffers == nullptr)
            {
                return true;
            }
            const RHI::BufferDesc* desc = buffers->GetDesc(scratch);
            return desc == nullptr || desc->SizeBytes >= requiredBytes;
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

    RHI::PipelineDesc BuildParallelPrefixScanPipelineDesc(const char* shaderPath)
    {
        RHI::PipelineDesc desc{};
        desc.ComputeShaderPath = shaderPath == nullptr ? "" : shaderPath;
        desc.PushConstantSize = static_cast<std::uint32_t>(
            sizeof(ParallelPrefixScanPushConstants));
        desc.DebugName = "ParallelPrimitive.PrefixScan";
        return desc;
    }

    RHI::PipelineDesc BuildParallelScanAddOffsetsPipelineDesc(const char* shaderPath)
    {
        RHI::PipelineDesc desc{};
        desc.ComputeShaderPath = shaderPath == nullptr ? "" : shaderPath;
        desc.PushConstantSize = static_cast<std::uint32_t>(
            sizeof(ParallelScanAddOffsetsPushConstants));
        desc.DebugName = "ParallelPrimitive.ScanAddOffsets";
        return desc;
    }

    RHI::PipelineDesc BuildParallelCompactByFlagsPipelineDesc(const char* shaderPath)
    {
        RHI::PipelineDesc desc{};
        desc.ComputeShaderPath = shaderPath == nullptr ? "" : shaderPath;
        desc.PushConstantSize = static_cast<std::uint32_t>(
            sizeof(ParallelCompactByFlagsPushConstants));
        desc.DebugName = "ParallelPrimitive.CompactByFlags";
        return desc;
    }

    GpuParallelPrimitiveRecordResult RecordGpuPrefixScan(
        const GpuPrefixScanRecordDesc& desc)
    {
        if (desc.Device == nullptr)
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidInput,
                                ParallelPrimitiveKind::PrefixScan,
                                desc.ElementCount);
        }

        if (!desc.Device->IsOperational())
        {
            return DeviceUnavailableResult(ParallelPrimitiveKind::PrefixScan,
                                           desc.ElementCount);
        }

        ParallelPrimitiveDispatchPlan plan =
            ComputePrefixScanDispatchPlan(desc.ElementCount, desc.Mode);
        if (!plan.IsValid())
        {
            return StatusResult(plan.Status,
                                ParallelPrimitiveKind::PrefixScan,
                                desc.ElementCount);
        }

        if (plan.Dispatches.empty())
        {
            return GpuParallelPrimitiveRecordResult{
                .Status = ParallelPrimitiveStatus::Success,
                .Kind = ParallelPrimitiveKind::PrefixScan,
                .Diagnostics = ParallelPrimitiveDiagnostics{
                    .ElementCount = desc.ElementCount,
                    .OutputCount = desc.ElementCount,
                },
                .Recorded = false,
                .CpuFallbackRecommended = false,
                .Scratch = {},
                .Plan = std::move(plan),
            };
        }

        if (desc.CommandContext == nullptr)
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidInput,
                                ParallelPrimitiveKind::PrefixScan,
                                desc.ElementCount);
        }

        if (!desc.Input.IsValid() || !desc.Output.IsValid())
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidGpuResource,
                                ParallelPrimitiveKind::PrefixScan,
                                desc.ElementCount);
        }

        GpuParallelPrimitiveRecordResult result{};
        result.Status = ParallelPrimitiveStatus::Success;
        result.Kind = ParallelPrimitiveKind::PrefixScan;
        result.Diagnostics = ParallelPrimitiveDiagnostics{
            .ElementCount = desc.ElementCount,
            .OutputCount = desc.ElementCount,
        };

        RHI::BufferHandle scratch = desc.Scratch;
        if (plan.ScratchBytes > 0u && !scratch.IsValid())
        {
            if (desc.Buffers == nullptr)
            {
                return StatusResult(ParallelPrimitiveStatus::InvalidInput,
                                    ParallelPrimitiveKind::PrefixScan,
                                    desc.ElementCount);
            }

            auto scratchOr = desc.Buffers->Create(
                BuildParallelPrimitiveScratchBufferDesc(plan));
            if (!scratchOr.has_value())
            {
                return StatusResult(ParallelPrimitiveStatus::InvalidGpuResource,
                                    ParallelPrimitiveKind::PrefixScan,
                                    desc.ElementCount);
            }

            result.ScratchLease = std::move(*scratchOr);
            scratch = result.ScratchLease.GetHandle();
        }

        if (!ScratchBufferIsLargeEnough(desc.Buffers, scratch, plan.ScratchBytes))
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidGpuResource,
                                ParallelPrimitiveKind::PrefixScan,
                                desc.ElementCount);
        }

        const RoleBindings bindings{
            .Input = desc.Input,
            .Output = desc.Output,
            .Scratch = scratch,
            .InputBDA = desc.Device->GetBufferDeviceAddress(desc.Input),
            .OutputBDA = desc.Device->GetBufferDeviceAddress(desc.Output),
            .ScratchBDA = scratch.IsValid()
                ? desc.Device->GetBufferDeviceAddress(scratch)
                : 0u,
        };

        if (!PlanResourcesAreRecordable(plan, bindings, desc.Pipelines))
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidGpuResource,
                                ParallelPrimitiveKind::PrefixScan,
                                desc.ElementCount);
        }

        RecordPlanDispatches(*desc.CommandContext,
                             plan,
                             bindings,
                             desc.Pipelines);

        result.Recorded = true;
        result.Scratch = scratch;
        result.Plan = std::move(plan);
        return result;
    }

    GpuParallelPrimitiveRecordResult RecordGpuStreamCompaction(
        const GpuStreamCompactionRecordDesc& desc)
    {
        if (desc.Device == nullptr)
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidInput,
                                ParallelPrimitiveKind::StreamCompaction,
                                desc.ElementCount);
        }

        if (!desc.Device->IsOperational())
        {
            return DeviceUnavailableResult(ParallelPrimitiveKind::StreamCompaction,
                                           desc.ElementCount);
        }

        ParallelPrimitiveDispatchPlan plan =
            ComputeStreamCompactionDispatchPlan(desc.ElementCount);
        if (!plan.IsValid())
        {
            return StatusResult(plan.Status,
                                ParallelPrimitiveKind::StreamCompaction,
                                desc.ElementCount);
        }

        if (desc.CommandContext == nullptr)
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidInput,
                                ParallelPrimitiveKind::StreamCompaction,
                                desc.ElementCount);
        }

        if (!desc.OutputCount.IsValid())
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidGpuResource,
                                ParallelPrimitiveKind::StreamCompaction,
                                desc.ElementCount);
        }

        if (desc.ElementCount == 0u)
        {
            desc.CommandContext->FillBuffer(desc.OutputCount,
                                            0u,
                                            sizeof(std::uint32_t),
                                            0u);
            desc.CommandContext->BufferBarrier(desc.OutputCount,
                                               RHI::MemoryAccess::TransferWrite,
                                               RHI::MemoryAccess::ShaderRead);
            return GpuParallelPrimitiveRecordResult{
                .Status = ParallelPrimitiveStatus::Success,
                .Kind = ParallelPrimitiveKind::StreamCompaction,
                .Diagnostics = ParallelPrimitiveDiagnostics{},
                .Recorded = true,
                .CpuFallbackRecommended = false,
                .Scratch = {},
                .Plan = std::move(plan),
            };
        }

        if (!desc.Keys.IsValid() ||
            !desc.Flags.IsValid() ||
            !desc.OutputKeys.IsValid())
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidGpuResource,
                                ParallelPrimitiveKind::StreamCompaction,
                                desc.ElementCount);
        }

        GpuParallelPrimitiveRecordResult result{};
        result.Status = ParallelPrimitiveStatus::Success;
        result.Kind = ParallelPrimitiveKind::StreamCompaction;
        result.Diagnostics.ElementCount = desc.ElementCount;

        RHI::BufferHandle scratch = desc.Scratch;
        if (plan.ScratchBytes > 0u && !scratch.IsValid())
        {
            if (desc.Buffers == nullptr)
            {
                return StatusResult(ParallelPrimitiveStatus::InvalidInput,
                                    ParallelPrimitiveKind::StreamCompaction,
                                    desc.ElementCount);
            }

            auto scratchOr = desc.Buffers->Create(
                BuildParallelPrimitiveScratchBufferDesc(plan));
            if (!scratchOr.has_value())
            {
                return StatusResult(ParallelPrimitiveStatus::InvalidGpuResource,
                                    ParallelPrimitiveKind::StreamCompaction,
                                    desc.ElementCount);
            }

            result.ScratchLease = std::move(*scratchOr);
            scratch = result.ScratchLease.GetHandle();
        }

        if (!ScratchBufferIsLargeEnough(desc.Buffers, scratch, plan.ScratchBytes))
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidGpuResource,
                                ParallelPrimitiveKind::StreamCompaction,
                                desc.ElementCount);
        }

        const RoleBindings bindings{
            .Keys = desc.Keys,
            .Flags = desc.Flags,
            .OutputKeys = desc.OutputKeys,
            .OutputCount = desc.OutputCount,
            .Scratch = scratch,
            .KeysBDA = desc.Device->GetBufferDeviceAddress(desc.Keys),
            .FlagsBDA = desc.Device->GetBufferDeviceAddress(desc.Flags),
            .OutputKeysBDA = desc.Device->GetBufferDeviceAddress(desc.OutputKeys),
            .OutputCountBDA = desc.Device->GetBufferDeviceAddress(desc.OutputCount),
            .ScratchBDA = scratch.IsValid()
                ? desc.Device->GetBufferDeviceAddress(scratch)
                : 0u,
        };

        if (!PlanResourcesAreRecordable(plan, bindings, desc.Pipelines))
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidGpuResource,
                                ParallelPrimitiveKind::StreamCompaction,
                                desc.ElementCount);
        }

        RecordPlanDispatches(*desc.CommandContext,
                             plan,
                             bindings,
                             desc.Pipelines);

        result.Recorded = true;
        result.Scratch = scratch;
        result.Plan = std::move(plan);
        return result;
    }
}
