module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

module Extrinsic.Runtime.ProgressivePoissonGpuBackend;

import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] constexpr std::uint32_t CeilDiv(
            const std::uint32_t value,
            const std::uint32_t divisor) noexcept
        {
            return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
        }

        [[nodiscard]] constexpr std::uint64_t FloatBytes(
            const std::uint32_t count) noexcept
        {
            return static_cast<std::uint64_t>(count) * sizeof(float);
        }

        [[nodiscard]] constexpr std::uint64_t Uint32Bytes(
            const std::uint32_t count) noexcept
        {
            return static_cast<std::uint64_t>(count) * sizeof(std::uint32_t);
        }

        [[nodiscard]] constexpr std::uint64_t Uint64Bytes(
            const std::uint32_t count) noexcept
        {
            return static_cast<std::uint64_t>(count) * sizeof(std::uint64_t);
        }

        [[nodiscard]] bool AddWouldOverflow(
            const std::uint64_t lhs,
            const std::uint64_t rhs) noexcept
        {
            return lhs > std::numeric_limits<std::uint64_t>::max() - rhs;
        }

        [[nodiscard]] bool AppendSpan(
            ProgressivePoissonGpuBufferLayout& layout,
            const ProgressivePoissonGpuBufferRole role,
            const std::uint64_t sizeBytes) noexcept
        {
            if (AddWouldOverflow(layout.WorkBufferBytes, sizeBytes))
            {
                return false;
            }

            layout.Spans.push_back(ProgressivePoissonGpuBufferSpan{
                .Role = role,
                .OffsetBytes = layout.WorkBufferBytes,
                .SizeBytes = sizeBytes,
            });
            layout.WorkBufferBytes += sizeBytes;
            return true;
        }

        [[nodiscard]] std::uint32_t PhaseCountForDimension(
            const std::uint32_t dimension) noexcept
        {
            return dimension == 3u ? 8u : 4u;
        }

        [[nodiscard]] std::uint32_t HashCapacityFor(
            const std::uint32_t inputCount,
            const float hashLoadFactor,
            bool& overflow) noexcept
        {
            overflow = false;
            if (inputCount == 0u)
            {
                return 0u;
            }

            const double capacity =
                std::ceil(static_cast<double>(inputCount) /
                          static_cast<double>(hashLoadFactor));
            if (!(capacity > 0.0) ||
                capacity >
                    static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
            {
                overflow = true;
                return 0u;
            }
            return std::max(1u, static_cast<std::uint32_t>(capacity));
        }

        [[nodiscard]] bool ConfigIsValid(
            const ProgressivePoissonGpuPlanDesc& desc) noexcept
        {
            return (desc.Config.Dimension == 2u || desc.Config.Dimension == 3u) &&
                   desc.Config.GridWidth > 0u &&
                   desc.Config.MaxLevels > 0u &&
                   desc.GroupSize > 0u &&
                   std::isfinite(desc.Config.HashLoadFactor) &&
                   desc.Config.HashLoadFactor > 0.0f;
        }

        [[nodiscard]] ProgressivePoissonGpuBufferLayout BuildLayout(
            const std::uint32_t inputCount,
            const std::uint32_t maxLevels,
            const std::uint32_t hashCapacity,
            const std::uint64_t compactionScratchBytes,
            bool& overflow)
        {
            overflow = false;
            ProgressivePoissonGpuBufferLayout layout{};
            layout.InputCount = inputCount;
            layout.MaxLevels = maxLevels;
            layout.HashTableCapacity = hashCapacity;
            layout.StateBytes = sizeof(ProgressivePoissonGpuStateBufferRecord);
            layout.CompactionScratchBytes = compactionScratchBytes;

            const auto add =
                [&layout, &overflow](const ProgressivePoissonGpuBufferRole role,
                                     const std::uint64_t sizeBytes) noexcept
                {
                    if (!AppendSpan(layout, role, sizeBytes))
                    {
                        overflow = true;
                    }
                };

            add(ProgressivePoissonGpuBufferRole::PositionX, FloatBytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::PositionY, FloatBytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::PositionZ, FloatBytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::RemainingKeys, Uint32Bytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::NextRemainingKeys,
                Uint32Bytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::AcceptedKeys, Uint32Bytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::CellKeys, Uint64Bytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::CellPhases, Uint32Bytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::AcceptFlags, Uint32Bytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::HashKeys, Uint64Bytes(hashCapacity));
            add(ProgressivePoissonGpuBufferRole::HashValues,
                Uint32Bytes(hashCapacity));
            add(ProgressivePoissonGpuBufferRole::LevelOffsets,
                Uint32Bytes(maxLevels + 1u));
            add(ProgressivePoissonGpuBufferRole::SplatRadii, FloatBytes(inputCount));
            add(ProgressivePoissonGpuBufferRole::OutputCount,
                Uint32Bytes(maxLevels + 1u));
            add(ProgressivePoissonGpuBufferRole::CompactionScratch,
                compactionScratchBytes);
            return layout;
        }

        [[nodiscard]] ProgressivePoissonGpuDispatchDesc MakeMethodDispatch(
            const ProgressivePoissonGpuPassKind kind,
            const std::uint32_t levelIndex,
            const std::uint32_t phaseIndex,
            const std::uint32_t elementCount,
            const std::uint32_t hashCapacity,
            const std::uint32_t groupSize) noexcept
        {
            return ProgressivePoissonGpuDispatchDesc{
                .Kind = kind,
                .LevelIndex = levelIndex,
                .PhaseIndex = phaseIndex,
                .ElementCount = elementCount,
                .HashTableCapacity = hashCapacity,
                .GroupSize = groupSize,
                .GroupCountX = CeilDiv(std::max(elementCount, hashCapacity),
                                        groupSize),
                .GroupCountY = 1u,
                .GroupCountZ = 1u,
            };
        }

        [[nodiscard]] ProgressivePoissonGpuResolveResult ResolveStatus(
            const ProgressivePoissonGpuStatus status,
            std::string diagnostic)
        {
            return ProgressivePoissonGpuResolveResult{
                .Status = status,
                .GpuExecutionAvailable = false,
                .CpuFallbackRecommended = true,
                .Diagnostic = std::move(diagnostic),
            };
        }
    }

    const char* DebugNameForProgressivePoissonGpuStatus(
        const ProgressivePoissonGpuStatus status) noexcept
    {
        switch (status)
        {
        case ProgressivePoissonGpuStatus::Success:
            return "Success";
        case ProgressivePoissonGpuStatus::InvalidInput:
            return "InvalidInput";
        case ProgressivePoissonGpuStatus::MissingDevice:
            return "MissingDevice";
        case ProgressivePoissonGpuStatus::DeviceUnavailable:
            return "DeviceUnavailable";
        case ProgressivePoissonGpuStatus::PlanningOnly:
            return "PlanningOnly";
        case ProgressivePoissonGpuStatus::SizeOverflow:
            return "SizeOverflow";
        }
        return "Unknown";
    }

    const char* DebugNameForProgressivePoissonGpuPassKind(
        const ProgressivePoissonGpuPassKind kind) noexcept
    {
        switch (kind)
        {
        case ProgressivePoissonGpuPassKind::BuildLevelCells:
            return "BuildLevelCells";
        case ProgressivePoissonGpuPassKind::AcceptPhase:
            return "AcceptPhase";
        case ProgressivePoissonGpuPassKind::CompactAccepted:
            return "CompactAccepted";
        case ProgressivePoissonGpuPassKind::CompactRemaining:
            return "CompactRemaining";
        }
        return "Unknown";
    }

    const char* DebugNameForProgressivePoissonGpuBufferRole(
        const ProgressivePoissonGpuBufferRole role) noexcept
    {
        switch (role)
        {
        case ProgressivePoissonGpuBufferRole::None:
            return "None";
        case ProgressivePoissonGpuBufferRole::State:
            return "State";
        case ProgressivePoissonGpuBufferRole::PositionX:
            return "PositionX";
        case ProgressivePoissonGpuBufferRole::PositionY:
            return "PositionY";
        case ProgressivePoissonGpuBufferRole::PositionZ:
            return "PositionZ";
        case ProgressivePoissonGpuBufferRole::RemainingKeys:
            return "RemainingKeys";
        case ProgressivePoissonGpuBufferRole::NextRemainingKeys:
            return "NextRemainingKeys";
        case ProgressivePoissonGpuBufferRole::AcceptedKeys:
            return "AcceptedKeys";
        case ProgressivePoissonGpuBufferRole::CellKeys:
            return "CellKeys";
        case ProgressivePoissonGpuBufferRole::CellPhases:
            return "CellPhases";
        case ProgressivePoissonGpuBufferRole::AcceptFlags:
            return "AcceptFlags";
        case ProgressivePoissonGpuBufferRole::HashKeys:
            return "HashKeys";
        case ProgressivePoissonGpuBufferRole::HashValues:
            return "HashValues";
        case ProgressivePoissonGpuBufferRole::LevelOffsets:
            return "LevelOffsets";
        case ProgressivePoissonGpuBufferRole::SplatRadii:
            return "SplatRadii";
        case ProgressivePoissonGpuBufferRole::OutputCount:
            return "OutputCount";
        case ProgressivePoissonGpuBufferRole::CompactionScratch:
            return "CompactionScratch";
        }
        return "Unknown";
    }

    ProgressivePoissonGpuDispatchPlan ComputeProgressivePoissonGpuDispatchPlan(
        const ProgressivePoissonGpuPlanDesc& desc)
    {
        ProgressivePoissonGpuDispatchPlan plan{};
        plan.InputCount = desc.InputCount;
        plan.Dimension = desc.Config.Dimension;
        plan.GroupSize = desc.GroupSize;
        plan.PhaseCount = PhaseCountForDimension(desc.Config.Dimension);

        if (!ConfigIsValid(desc))
        {
            plan.Status = ProgressivePoissonGpuStatus::InvalidInput;
            return plan;
        }

        bool hashOverflow = false;
        const std::uint32_t hashCapacity =
            HashCapacityFor(desc.InputCount,
                            desc.Config.HashLoadFactor,
                            hashOverflow);
        if (hashOverflow)
        {
            plan.Status = ProgressivePoissonGpuStatus::SizeOverflow;
            return plan;
        }

        const Graphics::ParallelPrimitiveDispatchPlan compactionPlan =
            Graphics::ComputeStreamCompactionDispatchPlan(desc.InputCount,
                                                          desc.GroupSize);
        if (!compactionPlan.IsValid())
        {
            plan.Status = ProgressivePoissonGpuStatus::InvalidInput;
            return plan;
        }

        bool layoutOverflow = false;
        plan.Layout = BuildLayout(desc.InputCount,
                                  desc.Config.MaxLevels,
                                  hashCapacity,
                                  compactionPlan.ScratchBytes,
                                  layoutOverflow);
        if (layoutOverflow)
        {
            plan.Status = ProgressivePoissonGpuStatus::SizeOverflow;
            return plan;
        }

        if (desc.InputCount == 0u)
        {
            return plan;
        }

        for (std::uint32_t level = 0u; level < desc.Config.MaxLevels; ++level)
        {
            ProgressivePoissonGpuLevelPlan levelPlan{};
            levelPlan.LevelIndex = level;
            levelPlan.PhaseCount = plan.PhaseCount;
            levelPlan.RemainingUpperBound = desc.InputCount;
            levelPlan.HashTableCapacity = hashCapacity;
            levelPlan.BuildCells = MakeMethodDispatch(
                ProgressivePoissonGpuPassKind::BuildLevelCells,
                level,
                0u,
                desc.InputCount,
                hashCapacity,
                desc.GroupSize);
            levelPlan.AcceptedCompaction = compactionPlan;
            levelPlan.RemainingCompaction = compactionPlan;

            plan.Dispatches.push_back(levelPlan.BuildCells);
            for (std::uint32_t phase = 0u; phase < plan.PhaseCount; ++phase)
            {
                ProgressivePoissonGpuDispatchDesc accept = MakeMethodDispatch(
                    ProgressivePoissonGpuPassKind::AcceptPhase,
                    level,
                    phase,
                    desc.InputCount,
                    hashCapacity,
                    desc.GroupSize);
                levelPlan.AcceptPhases.push_back(accept);
                plan.Dispatches.push_back(accept);
            }

            plan.Dispatches.push_back(MakeMethodDispatch(
                ProgressivePoissonGpuPassKind::CompactAccepted,
                level,
                0u,
                desc.InputCount,
                hashCapacity,
                desc.GroupSize));
            plan.Dispatches.push_back(MakeMethodDispatch(
                ProgressivePoissonGpuPassKind::CompactRemaining,
                level,
                0u,
                desc.InputCount,
                hashCapacity,
                desc.GroupSize));
            plan.Levels.push_back(std::move(levelPlan));
        }

        return plan;
    }

    RHI::BufferDesc BuildProgressivePoissonGpuStateBufferDesc(
        const char* debugName) noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = sizeof(ProgressivePoissonGpuStateBufferRecord),
            .Usage = RHI::BufferUsage::Storage |
                     RHI::BufferUsage::TransferSrc |
                     RHI::BufferUsage::TransferDst,
            .HostVisible = false,
            .DebugName = debugName,
        };
    }

    RHI::BufferDesc BuildProgressivePoissonGpuWorkBufferDesc(
        const ProgressivePoissonGpuBufferLayout& layout,
        const char* debugName) noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = layout.WorkBufferBytes,
            .Usage = RHI::BufferUsage::Storage |
                     RHI::BufferUsage::TransferSrc |
                     RHI::BufferUsage::TransferDst,
            .HostVisible = false,
            .DebugName = debugName,
        };
    }

    RHI::PipelineDesc BuildProgressivePoissonBuildCellsPipelineDesc(
        const char* shaderPath)
    {
        RHI::PipelineDesc desc{};
        desc.ComputeShaderPath = shaderPath == nullptr ? "" : shaderPath;
        desc.PushConstantSize = static_cast<std::uint32_t>(
            sizeof(ProgressivePoissonGpuPassPushConstants));
        desc.DebugName = "ProgressivePoisson.BuildCells";
        return desc;
    }

    RHI::PipelineDesc BuildProgressivePoissonAcceptPhasePipelineDesc(
        const char* shaderPath)
    {
        RHI::PipelineDesc desc{};
        desc.ComputeShaderPath = shaderPath == nullptr ? "" : shaderPath;
        desc.PushConstantSize = static_cast<std::uint32_t>(
            sizeof(ProgressivePoissonGpuPassPushConstants));
        desc.DebugName = "ProgressivePoisson.AcceptPhase";
        return desc;
    }

    ProgressivePoissonGpuResolveResult ResolveProgressivePoissonGpuRequest(
        const ProgressivePoissonGpuResolveDesc& desc)
    {
        if (desc.Device == nullptr)
        {
            return ResolveStatus(
                ProgressivePoissonGpuStatus::MissingDevice,
                "Vulkan compute requested but no RHI device was available.");
        }

        if (!desc.Device->IsOperational())
        {
            return ResolveStatus(
                ProgressivePoissonGpuStatus::DeviceUnavailable,
                "Vulkan compute requested but the RHI device is not operational.");
        }

        ProgressivePoissonGpuDispatchPlan plan =
            ComputeProgressivePoissonGpuDispatchPlan(desc.Plan);
        if (!plan.IsValid())
        {
            return ProgressivePoissonGpuResolveResult{
                .Status = plan.Status,
                .GpuExecutionAvailable = false,
                .CpuFallbackRecommended = true,
                .Diagnostic =
                    std::string{"Vulkan compute planning failed: "} +
                    DebugNameForProgressivePoissonGpuStatus(plan.Status) + ".",
                .Plan = std::move(plan),
            };
        }

        return ProgressivePoissonGpuResolveResult{
            .Status = ProgressivePoissonGpuStatus::PlanningOnly,
            .GpuExecutionAvailable = false,
            .CpuFallbackRecommended = true,
            .Diagnostic =
                "Vulkan compute dispatch planning is available, but METHOD-013 "
                "has not enabled executable GPU sampling yet.",
            .Plan = std::move(plan),
        };
    }
}
