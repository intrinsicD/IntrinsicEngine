module;

#include <cstdint>
#include <algorithm>
#include <limits>
#include <numeric>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Graphics.ComputeParallelPrimitives;

import Extrinsic.Core.Filesystem.PathResolver;
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

        // Scans `scanCount` uint32 values through plan.ScratchLevels (built for that count).
        void AppendPrefixScanDispatches(ParallelPrimitiveDispatchPlan& plan,
                                        const std::uint32_t scanCount,
                                        const ParallelPrimitiveBufferRole inputRole,
                                        const ParallelPrimitiveBufferRole outputRole,
                                        const std::uint64_t inputOffsetBytes,
                                        const std::uint64_t outputOffsetBytes,
                                        const std::uint64_t levelBaseOffsetBytes)
        {
            if (scanCount == 0u)
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
                .ElementCount = scanCount,
                .GroupSize = plan.GroupSize,
                .GroupCountX = CeilDiv(scanCount, plan.GroupSize),
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
                ParallelPrimitiveBufferRole blockSumsRole =
                    ParallelPrimitiveBufferRole::None;
                std::uint64_t blockSumsOffsetBytes =
                    kParallelPrimitiveInvalidOffset;
                if (hasNextLevel)
                {
                    blockSumsRole = ParallelPrimitiveBufferRole::Scratch;
                    blockSumsOffsetBytes =
                        plan.ScratchLevels[level + 1u].OffsetBytes;
                }
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
                    .BlockSumsRole = blockSumsRole,
                    .InputOffsetBytes = scratch.OffsetBytes,
                    .OutputOffsetBytes = scratch.OffsetBytes,
                    .BlockSumsOffsetBytes = blockSumsOffsetBytes,
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
                    .ElementCount = scanCount,
                    .GroupSize = plan.GroupSize,
                    .GroupCountX = CeilDiv(scanCount, plan.GroupSize),
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

        [[nodiscard]] GpuCompactionCountPublicationResult CountPublicationStatusResult(
            const ParallelPrimitiveStatus status,
            const bool cpuFallbackRecommended = false) noexcept
        {
            return GpuCompactionCountPublicationResult{
                .Status = status,
                .CpuFallbackRecommended = cpuFallbackRecommended,
            };
        }

        struct RoleBindings
        {
            RHI::BufferHandle Input{};
            RHI::BufferHandle Output{};
            RHI::BufferHandle Keys{};
            RHI::BufferHandle Flags{};
            RHI::BufferHandle OutputKeys{};
            RHI::BufferHandle OutputCount{};
            RHI::BufferHandle Values{};
            RHI::BufferHandle SegmentSums{};
            RHI::BufferHandle SegmentCounts{};
            RHI::BufferHandle SegmentMeans{};
            RHI::BufferHandle Scratch{};
            std::uint64_t InputBDA = 0u;
            std::uint64_t OutputBDA = 0u;
            std::uint64_t KeysBDA = 0u;
            std::uint64_t FlagsBDA = 0u;
            std::uint64_t OutputKeysBDA = 0u;
            std::uint64_t OutputCountBDA = 0u;
            std::uint64_t ValuesBDA = 0u;
            std::uint64_t SegmentSumsBDA = 0u;
            std::uint64_t SegmentCountsBDA = 0u;
            std::uint64_t SegmentMeansBDA = 0u;
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
            case ParallelPrimitiveBufferRole::Values:
                return bindings.Values;
            case ParallelPrimitiveBufferRole::SegmentSums:
                return bindings.SegmentSums;
            case ParallelPrimitiveBufferRole::SegmentCounts:
                return bindings.SegmentCounts;
            case ParallelPrimitiveBufferRole::SegmentMeans:
                return bindings.SegmentMeans;
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
            case ParallelPrimitiveBufferRole::Values:
                base = bindings.ValuesBDA;
                break;
            case ParallelPrimitiveBufferRole::SegmentSums:
                base = bindings.SegmentSumsBDA;
                break;
            case ParallelPrimitiveBufferRole::SegmentCounts:
                base = bindings.SegmentCountsBDA;
                break;
            case ParallelPrimitiveBufferRole::SegmentMeans:
                base = bindings.SegmentMeansBDA;
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
                   validAddress(dispatch.CountRole, dispatch.CountOffsetBytes) &&
                   validAddress(dispatch.ValuesRole, dispatch.ValuesOffsetBytes) &&
                   validAddress(dispatch.SumsRole, dispatch.SumsOffsetBytes) &&
                   validAddress(dispatch.CountsRole, dispatch.CountsOffsetBytes) &&
                   validAddress(dispatch.MeansRole, dispatch.MeansOffsetBytes);
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
            case ParallelPrimitivePassKind::SegmentedFloatReduce:
                return pipelines.SegmentedFloatReduce;
            case ParallelPrimitivePassKind::RadixHistogram:
                return pipelines.RadixHistogram;
            case ParallelPrimitivePassKind::RadixScatter:
                return pipelines.RadixScatter;
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
                if (dispatch.Kind == ParallelPrimitivePassKind::SegmentedFloatReduce &&
                    (bindings.SegmentSumsBDA == 0u ||
                     bindings.SegmentCountsBDA == 0u ||
                     bindings.SegmentMeansBDA == 0u))
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
                case ParallelPrimitivePassKind::SegmentedFloatReduce:
                {
                    const ParallelSegmentedFloatReducePushConstants pc{
                        .KeysBDA = AddressForRole(bindings,
                                                  dispatch.InputRole,
                                                  dispatch.InputOffsetBytes),
                        .ValuesBDA = AddressForRole(bindings,
                                                    dispatch.ValuesRole,
                                                    dispatch.ValuesOffsetBytes),
                        .SegmentSumsBDA = AddressForRole(bindings,
                                                         dispatch.SumsRole,
                                                         dispatch.SumsOffsetBytes),
                        .SegmentCountsBDA = AddressForRole(bindings,
                                                           dispatch.CountsRole,
                                                           dispatch.CountsOffsetBytes),
                        .SegmentMeansBDA = AddressForRole(bindings,
                                                          dispatch.MeansRole,
                                                          dispatch.MeansOffsetBytes),
                        .ElementCount = dispatch.ElementCount,
                        .SegmentCount = dispatch.SegmentCount,
                        .Reserved0 = 0u,
                        .Reserved1 = 0u,
                    };
                    cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)), 0u);
                    break;
                }
                case ParallelPrimitivePassKind::RadixHistogram:
                case ParallelPrimitivePassKind::RadixScatter:
                {
                    // Histogram: OutputRole receives the digit counts. Scatter: OffsetsRole holds
                    // their scan and OutputRole receives the records.
                    const bool scatter = dispatch.Kind == ParallelPrimitivePassKind::RadixScatter;
                    const ParallelRadixSortPushConstants pc{
                        .SourceBDA = AddressForRole(bindings,
                                                    dispatch.InputRole,
                                                    dispatch.InputOffsetBytes),
                        .DestinationBDA = scatter
                            ? AddressForRole(bindings, dispatch.OutputRole, dispatch.OutputOffsetBytes)
                            : 0u,
                        .DigitCountsBDA = scatter
                            ? AddressForRole(bindings, dispatch.OffsetsRole, dispatch.OffsetsOffsetBytes)
                            : AddressForRole(bindings, dispatch.OutputRole, dispatch.OutputOffsetBytes),
                        .ElementCount = dispatch.ElementCount,
                        .GroupCount = dispatch.GroupCountX,
                        .KeyWords = dispatch.KeyWords,
                        .DigitShift = dispatch.DigitShift,
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

        // Shared tail of every Record*: acquires (or checks) the scratch buffer, binds it,
        // validates the plan's resources and records its dispatches.
        [[nodiscard]] GpuParallelPrimitiveRecordResult RecordPlanWithScratch(
            RHI::IDevice& device,
            RHI::ICommandContext& cmd,
            RHI::BufferManager* buffers,
            const ParallelPrimitivePipelineSet& pipelines,
            RHI::BufferHandle scratch,
            ParallelPrimitiveDispatchPlan plan,
            RoleBindings bindings,
            GpuParallelPrimitiveRecordResult result)
        {
            const ParallelPrimitiveKind kind = result.Kind;
            const std::uint32_t count = result.Diagnostics.ElementCount;
            if (plan.ScratchBytes > 0u && !scratch.IsValid())
            {
                if (buffers == nullptr)
                {
                    return StatusResult(ParallelPrimitiveStatus::InvalidInput, kind, count);
                }
                auto scratchOr = buffers->Create(BuildParallelPrimitiveScratchBufferDesc(plan));
                if (!scratchOr.has_value())
                {
                    return StatusResult(ParallelPrimitiveStatus::InvalidGpuResource, kind, count);
                }
                result.ScratchLease = std::move(*scratchOr);
                scratch = result.ScratchLease.GetHandle();
            }

            if (!ScratchBufferIsLargeEnough(buffers, scratch, plan.ScratchBytes))
            {
                return StatusResult(ParallelPrimitiveStatus::InvalidGpuResource, kind, count);
            }

            bindings.Scratch = scratch;
            bindings.ScratchBDA = scratch.IsValid() ? device.GetBufferDeviceAddress(scratch) : 0u;
            if (!PlanResourcesAreRecordable(plan, bindings, pipelines))
            {
                return StatusResult(ParallelPrimitiveStatus::InvalidGpuResource, kind, count);
            }

            RecordPlanDispatches(cmd, plan, bindings, pipelines);
            result.Recorded = true;
            result.Scratch = scratch;
            result.Plan = std::move(plan);
            return result;
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
        case ParallelPrimitivePassKind::SegmentedFloatReduce:
            return "SegmentedFloatReduce";
        case ParallelPrimitivePassKind::RadixHistogram:
            return "RadixHistogram";
        case ParallelPrimitivePassKind::RadixScatter:
            return "RadixScatter";
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
        case ParallelPrimitiveBufferRole::Values:
            return "Values";
        case ParallelPrimitiveBufferRole::SegmentSums:
            return "SegmentSums";
        case ParallelPrimitiveBufferRole::SegmentCounts:
            return "SegmentCounts";
        case ParallelPrimitiveBufferRole::SegmentMeans:
            return "SegmentMeans";
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

    ParallelPrimitiveCpuResult ReduceFloatBySegmentCpu(
        const std::span<const std::uint32_t> keys,
        const std::span<const float> values,
        const std::uint32_t segmentCount,
        const std::span<float> segmentSums,
        const std::span<std::uint32_t> segmentCounts,
        const std::span<float> segmentMeans) noexcept
    {
        ParallelPrimitiveCpuResult result{};
        result.Diagnostics.ElementCount = static_cast<std::uint32_t>(keys.size());
        result.Diagnostics.OutputCount = segmentCount;

        if (keys.size() != values.size())
        {
            result.Status = ParallelPrimitiveStatus::SizeMismatch;
            result.Diagnostics.OutputCount = static_cast<std::uint32_t>(
                std::min(segmentSums.size(),
                         std::min(segmentCounts.size(), segmentMeans.size())));
            return result;
        }

        if (segmentCount == 0u)
        {
            result.Status = ParallelPrimitiveStatus::InvalidInput;
            result.Diagnostics.OutputCount = 0u;
            return result;
        }

        if (segmentSums.size() < segmentCount ||
            segmentCounts.size() < segmentCount ||
            segmentMeans.size() < segmentCount)
        {
            result.Status = ParallelPrimitiveStatus::OutputTooSmall;
            result.Diagnostics.OutputCount = static_cast<std::uint32_t>(
                std::min(segmentSums.size(),
                         std::min(segmentCounts.size(), segmentMeans.size())));
            return result;
        }

        for (std::uint32_t segment = 0u; segment < segmentCount; ++segment)
        {
            segmentSums[segment] = 0.0f;
            segmentCounts[segment] = 0u;
            segmentMeans[segment] =
                kParallelSegmentedFloatReductionMeanForEmptySegment;
        }

        std::uint64_t accumulatedCount = 0u;
        for (std::size_t index = 0u; index < keys.size(); ++index)
        {
            const std::uint32_t segment = keys[index];
            if (segment >= segmentCount)
            {
                result.Status = ParallelPrimitiveStatus::InvalidInput;
                result.Diagnostics.FirstFailureIndex =
                    static_cast<std::uint32_t>(index);
                result.Diagnostics.Total = accumulatedCount;
                return result;
            }

            segmentSums[segment] += values[index];
            ++segmentCounts[segment];
            ++accumulatedCount;
        }

        for (std::uint32_t segment = 0u; segment < segmentCount; ++segment)
        {
            if (segmentCounts[segment] != 0u)
            {
                segmentMeans[segment] =
                    segmentSums[segment] / static_cast<float>(segmentCounts[segment]);
            }
        }

        result.Diagnostics.Total = accumulatedCount;
        return result;
    }

    ParallelPrimitiveCpuResult SortRecordsByKeyCpu(
        const std::span<std::uint32_t> records,
        const std::uint32_t keyWords) noexcept
    {
        ParallelPrimitiveCpuResult result{};
        const std::size_t stride = std::size_t(keyWords) + 1u;
        if (keyWords < 1u || keyWords > 2u || records.size() % stride != 0u ||
            records.size() / stride > kParallelRadixMaxElements)
        {
            result.Status = ParallelPrimitiveStatus::InvalidInput;
            return result;
        }
        const std::size_t count = records.size() / stride;
        const auto key = [&](const std::size_t record)
        {
            std::uint64_t value = records[record * stride];
            if (keyWords == 2u) value |= std::uint64_t(records[record * stride + 1u]) << 32u;
            return value;
        };
        std::vector<std::uint32_t> order(count);
        std::iota(order.begin(), order.end(), 0u);
        std::ranges::stable_sort(order, {}, [&](const std::uint32_t record) { return key(record); });
        const std::vector<std::uint32_t> input(records.begin(), records.end());
        for (std::size_t i = 0; i < count; ++i)
            std::copy_n(input.begin() + std::ptrdiff_t(order[i] * stride), stride,
                        records.begin() + std::ptrdiff_t(i * stride));
        result.Diagnostics.ElementCount = static_cast<std::uint32_t>(count);
        result.Diagnostics.OutputCount = static_cast<std::uint32_t>(count);
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
                                   elementCount,
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
                                   elementCount,
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

    ParallelPrimitiveDispatchPlan ComputeSegmentedFloatReductionDispatchPlan(
        const std::uint32_t elementCount,
        const std::uint32_t segmentCount,
        const std::uint32_t groupSize)
    {
        ParallelPrimitiveDispatchPlan plan{};
        plan.Kind = ParallelPrimitiveKind::SegmentedFloatReduction;
        plan.ElementCount = elementCount;
        plan.SegmentCount = segmentCount;
        plan.GroupSize = groupSize;

        if (groupSize == 0u || segmentCount == 0u)
        {
            plan.Status = ParallelPrimitiveStatus::InvalidInput;
            return plan;
        }

        plan.Dispatches.push_back(ParallelPrimitiveDispatchDesc{
            .Kind = ParallelPrimitivePassKind::SegmentedFloatReduce,
            .Mode = PrefixScanMode::Exclusive,
            .LevelIndex = 0u,
            .ElementCount = elementCount,
            .SegmentCount = segmentCount,
            .GroupSize = groupSize,
            .GroupCountX = segmentCount,
            .GroupCountY = 1u,
            .GroupCountZ = 1u,
            .InputRole = elementCount > 0u
                ? ParallelPrimitiveBufferRole::Keys
                : ParallelPrimitiveBufferRole::None,
            .ValuesRole = elementCount > 0u
                ? ParallelPrimitiveBufferRole::Values
                : ParallelPrimitiveBufferRole::None,
            .SumsRole = ParallelPrimitiveBufferRole::SegmentSums,
            .CountsRole = ParallelPrimitiveBufferRole::SegmentCounts,
            .MeansRole = ParallelPrimitiveBufferRole::SegmentMeans,
        });

        AddShaderReadBarrier(plan, 0u, ParallelPrimitiveBufferRole::SegmentSums);
        AddShaderReadBarrier(plan, 0u, ParallelPrimitiveBufferRole::SegmentCounts);
        AddShaderReadBarrier(plan, 0u, ParallelPrimitiveBufferRole::SegmentMeans);
        return plan;
    }

    ParallelPrimitiveDispatchPlan ComputeRadixSortDispatchPlan(
        const std::uint32_t elementCount,
        const std::uint32_t keyWords,
        const std::uint32_t keyBits,
        const std::uint32_t groupSize)
    {
        ParallelPrimitiveDispatchPlan plan{};
        plan.Kind = ParallelPrimitiveKind::RadixSort;
        plan.ElementCount = elementCount;
        plan.KeyWords = keyWords;
        plan.GroupSize = groupSize;
        // The shaders sort 256-record tiles; keys are one or two words.
        if (groupSize != kParallelPrimitiveGroupSize || keyWords < 1u || keyWords > 2u ||
            keyBits < 1u || keyBits > 32u * keyWords || elementCount > kParallelRadixMaxElements)
        {
            plan.Status = ParallelPrimitiveStatus::InvalidInput;
            return plan;
        }
        if (elementCount <= 1u)
        {
            return plan;
        }

        // Scratch: record copy | digit counts | their exclusive scan | scan levels.
        const std::uint32_t groups = CeilDiv(elementCount, groupSize);
        const std::uint32_t digitCounts = groups * (1u << kParallelRadixDigitBits);
        const std::uint64_t copyBytes =
            static_cast<std::uint64_t>(elementCount) * (keyWords + 1u) * sizeof(std::uint32_t);
        const std::uint64_t countsOffset = copyBytes;
        plan.PrefixOffsetsOffsetBytes = countsOffset + Uint32Bytes(digitCounts);
        plan.PrefixOffsetsSizeBytes = Uint32Bytes(digitCounts);
        const std::uint64_t levelsOffset = plan.PrefixOffsetsOffsetBytes + plan.PrefixOffsetsSizeBytes;
        plan.ScratchLevels = BuildScanScratchLevels(digitCounts, groupSize, levelsOffset);
        plan.ScratchBytes = EndOfScratchLevels(plan.ScratchLevels, levelsOffset);

        // An even pass count leaves the result in the caller's records (Keys role).
        std::uint32_t passes = CeilDiv(keyBits, kParallelRadixDigitBits);
        passes += passes % 2u;
        for (std::uint32_t pass = 0u; pass < passes; ++pass)
        {
            const bool fromKeys = pass % 2u == 0u;
            const auto source = fromKeys ? ParallelPrimitiveBufferRole::Keys : ParallelPrimitiveBufferRole::Scratch;
            const auto target = fromKeys ? ParallelPrimitiveBufferRole::Scratch : ParallelPrimitiveBufferRole::Keys;
            const std::uint64_t sourceOffset = fromKeys ? kParallelPrimitiveInvalidOffset : 0u;
            const std::uint64_t targetOffset = fromKeys ? 0u : kParallelPrimitiveInvalidOffset;
            const ParallelPrimitiveDispatchDesc pass0{
                .Kind = ParallelPrimitivePassKind::RadixHistogram,
                .ElementCount = elementCount,
                .KeyWords = keyWords,
                .DigitShift = pass * kParallelRadixDigitBits,
                .GroupSize = groupSize,
                .GroupCountX = groups,
                .InputRole = source,
                .OutputRole = ParallelPrimitiveBufferRole::Scratch,
                .InputOffsetBytes = sourceOffset,
                .OutputOffsetBytes = countsOffset,
            };
            plan.Dispatches.push_back(pass0);
            AddScratchBarrier(plan, static_cast<std::uint32_t>(plan.Dispatches.size() - 1u));
            AppendPrefixScanDispatches(plan,
                                       digitCounts,
                                       ParallelPrimitiveBufferRole::Scratch,
                                       ParallelPrimitiveBufferRole::Scratch,
                                       countsOffset,
                                       plan.PrefixOffsetsOffsetBytes,
                                       plan.ScratchLevels.empty()
                                           ? kParallelPrimitiveInvalidOffset
                                           : plan.ScratchLevels.front().OffsetBytes);
            AddScratchBarrier(plan, static_cast<std::uint32_t>(plan.Dispatches.size() - 1u));
            ParallelPrimitiveDispatchDesc scatter = pass0;
            scatter.Kind = ParallelPrimitivePassKind::RadixScatter;
            scatter.OutputRole = target;
            scatter.OutputOffsetBytes = targetOffset;
            scatter.OffsetsRole = ParallelPrimitiveBufferRole::Scratch;
            scatter.OffsetsOffsetBytes = plan.PrefixOffsetsOffsetBytes;
            plan.Dispatches.push_back(scatter);
            const auto index = static_cast<std::uint32_t>(plan.Dispatches.size() - 1u);
            for (const auto role : {ParallelPrimitiveBufferRole::Keys, ParallelPrimitiveBufferRole::Scratch})
            {
                plan.Barriers.push_back(ParallelPrimitiveBarrierDesc{
                    .AfterDispatchIndex = index,
                    .Buffer = role,
                    .Before = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                    .After = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite,
                });
            }
        }
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

    RHI::BufferDesc BuildParallelCompactionCountReadbackBufferDesc(
        const char* debugName) noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = sizeof(std::uint32_t),
            .Usage = RHI::BufferUsage::TransferDst |
                     RHI::BufferUsage::TransferSrc,
            .HostVisible = true,
            .DebugName = debugName,
        };
    }

    RHI::BufferDesc BuildParallelDispatchIndirectArgsBufferDesc(
        const char* debugName) noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = sizeof(ParallelDispatchIndirectArgs),
            .Usage = RHI::BufferUsage::Storage |
                     RHI::BufferUsage::Indirect |
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

    RHI::PipelineDesc BuildParallelCountToDispatchArgsPipelineDesc(
        const char* shaderPath)
    {
        RHI::PipelineDesc desc{};
        desc.ComputeShaderPath = shaderPath == nullptr ? "" : shaderPath;
        desc.PushConstantSize = static_cast<std::uint32_t>(
            sizeof(ParallelCountToDispatchArgsPushConstants));
        desc.DebugName = "ParallelPrimitive.CountToDispatchArgs";
        return desc;
    }

    RHI::PipelineDesc BuildParallelSegmentedFloatReducePipelineDesc(
        const char* shaderPath)
    {
        RHI::PipelineDesc desc{};
        desc.ComputeShaderPath = shaderPath == nullptr ? "" : shaderPath;
        desc.PushConstantSize = static_cast<std::uint32_t>(
            sizeof(ParallelSegmentedFloatReducePushConstants));
        desc.DebugName = "ParallelPrimitive.SegmentedFloatReduce";
        return desc;
    }

    RHI::PipelineDesc BuildParallelRadixHistogramPipelineDesc(const char* shaderPath)
    {
        RHI::PipelineDesc desc{};
        desc.ComputeShaderPath = shaderPath == nullptr ? "" : shaderPath;
        desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(ParallelRadixSortPushConstants));
        desc.DebugName = "ParallelPrimitive.RadixHistogram";
        return desc;
    }

    RHI::PipelineDesc BuildParallelRadixScatterPipelineDesc(const char* shaderPath)
    {
        RHI::PipelineDesc desc{};
        desc.ComputeShaderPath = shaderPath == nullptr ? "" : shaderPath;
        desc.PushConstantSize = static_cast<std::uint32_t>(sizeof(ParallelRadixSortPushConstants));
        desc.DebugName = "ParallelPrimitive.RadixScatter";
        return desc;
    }

    RHI::PipelineHandle CreateComputePipeline(RHI::IDevice& device,
                                              const char* shaderPath,
                                              const std::uint32_t pushConstantSize,
                                              const char* debugName)
    {
        return device.CreatePipeline(RHI::PipelineDesc{
            .ComputeShaderPath = Core::Filesystem::GetShaderPath(shaderPath),
            .PushConstantSize = pushConstantSize,
            .DebugName = debugName});
    }

    bool CreateParallelPrimitivePipelines(RHI::IDevice& device,
                                          ParallelPrimitivePipelineSet& pipelines,
                                          const std::span<const ParallelPrimitiveKind> kinds)
    {
        const auto create = [&device](RHI::PipelineHandle& pipeline, RHI::PipelineDesc desc)
        {
            if (pipeline.IsValid()) return true;
            desc.ComputeShaderPath = Core::Filesystem::GetShaderPath(desc.ComputeShaderPath);
            pipeline = device.CreatePipeline(desc);
            return pipeline.IsValid();
        };
        bool created = true;
        for (const ParallelPrimitiveKind kind : kinds)
        {
            // Every primitive but the segmented reduction scans.
            if (kind != ParallelPrimitiveKind::SegmentedFloatReduction)
                created = created && create(pipelines.PrefixScan, BuildParallelPrefixScanPipelineDesc()) &&
                          create(pipelines.AddBlockOffsets, BuildParallelScanAddOffsetsPipelineDesc());
            if (kind == ParallelPrimitiveKind::StreamCompaction)
                created = created && create(pipelines.CompactByFlags, BuildParallelCompactByFlagsPipelineDesc());
            if (kind == ParallelPrimitiveKind::SegmentedFloatReduction)
                created = created &&
                          create(pipelines.SegmentedFloatReduce, BuildParallelSegmentedFloatReducePipelineDesc());
            if (kind == ParallelPrimitiveKind::RadixSort)
                created = created && create(pipelines.RadixHistogram, BuildParallelRadixHistogramPipelineDesc()) &&
                          create(pipelines.RadixScatter, BuildParallelRadixScatterPipelineDesc());
        }
        return created;
    }

    void DestroyParallelPrimitivePipelines(RHI::IDevice& device,
                                           ParallelPrimitivePipelineSet& pipelines) noexcept
    {
        for (RHI::PipelineHandle* pipeline : {&pipelines.PrefixScan, &pipelines.AddBlockOffsets,
                                              &pipelines.CompactByFlags, &pipelines.SegmentedFloatReduce,
                                              &pipelines.RadixHistogram, &pipelines.RadixScatter})
        {
            if (pipeline->IsValid()) device.DestroyPipeline(*pipeline);
            *pipeline = {};
        }
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

        RoleBindings bindings{
            .Input = desc.Input,
            .Output = desc.Output,
            .InputBDA = desc.Device->GetBufferDeviceAddress(desc.Input),
            .OutputBDA = desc.Device->GetBufferDeviceAddress(desc.Output),
        };

        return RecordPlanWithScratch(*desc.Device,
                                     *desc.CommandContext,
                                     desc.Buffers,
                                     desc.Pipelines,
                                     desc.Scratch,
                                     std::move(plan),
                                     bindings,
                                     std::move(result));
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

        RoleBindings bindings{
            .Keys = desc.Keys,
            .Flags = desc.Flags,
            .OutputKeys = desc.OutputKeys,
            .OutputCount = desc.OutputCount,
            .KeysBDA = desc.Device->GetBufferDeviceAddress(desc.Keys),
            .FlagsBDA = desc.Device->GetBufferDeviceAddress(desc.Flags),
            .OutputKeysBDA = desc.Device->GetBufferDeviceAddress(desc.OutputKeys),
            .OutputCountBDA = desc.Device->GetBufferDeviceAddress(desc.OutputCount),
        };

        return RecordPlanWithScratch(*desc.Device,
                                     *desc.CommandContext,
                                     desc.Buffers,
                                     desc.Pipelines,
                                     desc.Scratch,
                                     std::move(plan),
                                     bindings,
                                     std::move(result));
    }

    GpuParallelPrimitiveRecordResult RecordGpuSegmentedFloatReduction(
        const GpuSegmentedFloatReductionRecordDesc& desc)
    {
        if (desc.Device == nullptr)
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidInput,
                                ParallelPrimitiveKind::SegmentedFloatReduction,
                                desc.ElementCount);
        }

        if (!desc.Device->IsOperational())
        {
            return DeviceUnavailableResult(
                ParallelPrimitiveKind::SegmentedFloatReduction,
                desc.ElementCount);
        }

        ParallelPrimitiveDispatchPlan plan =
            ComputeSegmentedFloatReductionDispatchPlan(desc.ElementCount,
                                                       desc.SegmentCount);
        if (!plan.IsValid())
        {
            return StatusResult(plan.Status,
                                ParallelPrimitiveKind::SegmentedFloatReduction,
                                desc.ElementCount);
        }

        if (desc.CommandContext == nullptr)
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidInput,
                                ParallelPrimitiveKind::SegmentedFloatReduction,
                                desc.ElementCount);
        }

        if (!desc.SegmentSums.IsValid() ||
            !desc.SegmentCounts.IsValid() ||
            !desc.SegmentMeans.IsValid() ||
            (desc.ElementCount > 0u &&
             (!desc.Keys.IsValid() || !desc.Values.IsValid())))
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidGpuResource,
                                ParallelPrimitiveKind::SegmentedFloatReduction,
                                desc.ElementCount);
        }

        GpuParallelPrimitiveRecordResult result{};
        result.Status = ParallelPrimitiveStatus::Success;
        result.Kind = ParallelPrimitiveKind::SegmentedFloatReduction;
        result.Diagnostics = ParallelPrimitiveDiagnostics{
            .ElementCount = desc.ElementCount,
            .OutputCount = desc.SegmentCount,
            .Total = desc.ElementCount,
        };

        RoleBindings bindings{
            .Keys = desc.Keys,
            .Values = desc.Values,
            .SegmentSums = desc.SegmentSums,
            .SegmentCounts = desc.SegmentCounts,
            .SegmentMeans = desc.SegmentMeans,
            .KeysBDA = desc.Keys.IsValid()
                ? desc.Device->GetBufferDeviceAddress(desc.Keys)
                : 0u,
            .ValuesBDA = desc.Values.IsValid()
                ? desc.Device->GetBufferDeviceAddress(desc.Values)
                : 0u,
            .SegmentSumsBDA = desc.Device->GetBufferDeviceAddress(desc.SegmentSums),
            .SegmentCountsBDA = desc.Device->GetBufferDeviceAddress(desc.SegmentCounts),
            .SegmentMeansBDA = desc.Device->GetBufferDeviceAddress(desc.SegmentMeans),
        };

        return RecordPlanWithScratch(*desc.Device,
                                     *desc.CommandContext,
                                     desc.Buffers,
                                     desc.Pipelines,
                                     desc.Scratch,
                                     std::move(plan),
                                     bindings,
                                     std::move(result));
    }

    GpuParallelPrimitiveRecordResult RecordGpuRadixSort(
        const GpuRadixSortRecordDesc& desc)
    {
        constexpr auto kind = ParallelPrimitiveKind::RadixSort;
        if (desc.Device == nullptr)
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidInput, kind, desc.ElementCount);
        }
        if (!desc.Device->IsOperational())
        {
            return DeviceUnavailableResult(kind, desc.ElementCount);
        }
        ParallelPrimitiveDispatchPlan plan =
            ComputeRadixSortDispatchPlan(desc.ElementCount, desc.KeyWords, desc.KeyBits);
        if (!plan.IsValid())
        {
            return StatusResult(plan.Status, kind, desc.ElementCount);
        }
        GpuParallelPrimitiveRecordResult result{
            .Status = ParallelPrimitiveStatus::Success,
            .Kind = kind,
            .Diagnostics = ParallelPrimitiveDiagnostics{.ElementCount = desc.ElementCount,
                                                        .OutputCount = desc.ElementCount},
        };
        if (plan.Dispatches.empty())
        {
            result.Plan = std::move(plan);
            return result;
        }
        if (desc.CommandContext == nullptr)
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidInput, kind, desc.ElementCount);
        }
        const std::uint64_t elements =
            desc.Elements.IsValid() ? desc.Device->GetBufferDeviceAddress(desc.Elements) : 0u;
        if (elements == 0u || desc.ElementsOffsetBytes % sizeof(std::uint32_t) != 0u)
        {
            return StatusResult(ParallelPrimitiveStatus::InvalidGpuResource, kind, desc.ElementCount);
        }
        const RoleBindings bindings{.Keys = desc.Elements, .KeysBDA = elements + desc.ElementsOffsetBytes};
        return RecordPlanWithScratch(*desc.Device,
                                     *desc.CommandContext,
                                     desc.Buffers,
                                     desc.Pipelines,
                                     desc.Scratch,
                                     std::move(plan),
                                     bindings,
                                     std::move(result));
    }

    GpuCompactionCountPublicationResult RecordCompactionCountPublication(
        const GpuCompactionCountPublicationDesc& desc)
    {
        if (desc.Device == nullptr)
        {
            return CountPublicationStatusResult(ParallelPrimitiveStatus::InvalidInput);
        }

        if (!desc.Device->IsOperational())
        {
            return CountPublicationStatusResult(ParallelPrimitiveStatus::DeviceUnavailable,
                                                true);
        }

        if (desc.CommandContext == nullptr ||
            !desc.OutputCount.IsValid() ||
            (!desc.ReadbackCount.IsValid() && !desc.DispatchArgs.IsValid()))
        {
            return CountPublicationStatusResult(ParallelPrimitiveStatus::InvalidInput);
        }

        GpuCompactionCountPublicationResult result{};
        std::uint64_t countBDA = 0u;
        std::uint64_t dispatchArgsBDA = 0u;

        if (desc.DispatchArgs.IsValid())
        {
            if (!desc.CountToDispatchArgsPipeline.IsValid() ||
                desc.DispatchGroupSize == 0u)
            {
                return CountPublicationStatusResult(
                    ParallelPrimitiveStatus::InvalidInput);
            }

            countBDA = desc.Device->GetBufferDeviceAddress(desc.OutputCount);
            dispatchArgsBDA = desc.Device->GetBufferDeviceAddress(desc.DispatchArgs);
            if (countBDA == 0u || dispatchArgsBDA == 0u)
            {
                return CountPublicationStatusResult(
                    ParallelPrimitiveStatus::InvalidGpuResource);
            }
        }

        if (desc.ReadbackCount.IsValid())
        {
            desc.CommandContext->BufferBarrier(desc.OutputCount,
                                               RHI::MemoryAccess::ShaderRead,
                                               RHI::MemoryAccess::TransferRead);
            desc.CommandContext->CopyBuffer(desc.OutputCount,
                                            desc.ReadbackCount,
                                            desc.OutputCountOffsetBytes,
                                            desc.ReadbackCountOffsetBytes,
                                            sizeof(std::uint32_t));
            desc.CommandContext->BufferBarrier(desc.ReadbackCount,
                                               RHI::MemoryAccess::TransferWrite,
                                               RHI::MemoryAccess::HostRead);
            result.RecordedReadbackCopy = true;
            if (!desc.DispatchArgs.IsValid())
            {
                desc.CommandContext->BufferBarrier(desc.OutputCount,
                                                   RHI::MemoryAccess::TransferRead,
                                                   RHI::MemoryAccess::ShaderRead);
            }
        }

        if (desc.DispatchArgs.IsValid())
        {
            if (desc.ReadbackCount.IsValid())
            {
                desc.CommandContext->BufferBarrier(desc.OutputCount,
                                                   RHI::MemoryAccess::TransferRead,
                                                   RHI::MemoryAccess::ShaderRead);
            }

            const ParallelCountToDispatchArgsPushConstants pc{
                .CountBDA = countBDA + desc.OutputCountOffsetBytes,
                .DispatchArgsBDA = dispatchArgsBDA + desc.DispatchArgsOffsetBytes,
                .GroupSize = desc.DispatchGroupSize,
                .Reserved0 = 0u,
                .Reserved1 = 0u,
                .Reserved2 = 0u,
            };
            desc.CommandContext->BindPipeline(desc.CountToDispatchArgsPipeline);
            desc.CommandContext->PushConstants(
                &pc,
                static_cast<std::uint32_t>(sizeof(pc)),
                0u);
            desc.CommandContext->Dispatch(1u, 1u, 1u);
            desc.CommandContext->BufferBarrier(desc.DispatchArgs,
                                               RHI::MemoryAccess::ShaderWrite,
                                               RHI::MemoryAccess::IndirectRead);
            result.RecordedDispatchArgs = true;
        }

        return result;
    }
}
