module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

module Extrinsic.Runtime.KMeansGpuBackend;

import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;

namespace Extrinsic::Runtime
{
    namespace
    {
        // Fail-closed cap on the packed work buffer; a request past this is
        // rejected as SizeOverflow rather than attempting a giant allocation.
        constexpr std::uint64_t kMaxKMeansWorkBufferBytes = std::uint64_t{1} << 34; // 16 GiB

        [[nodiscard]] constexpr std::uint64_t AlignUp16(const std::uint64_t bytes) noexcept
        {
            return (bytes + 15u) & ~std::uint64_t{15u};
        }

        [[nodiscard]] constexpr std::uint32_t CeilDiv(const std::uint32_t value,
                                                      const std::uint32_t divisor) noexcept
        {
            return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
        }
    }

    const char* DebugNameForKMeansGpuStatus(const KMeansGpuStatus status) noexcept
    {
        switch (status)
        {
        case KMeansGpuStatus::Success: return "Success";
        case KMeansGpuStatus::InvalidInput: return "InvalidInput";
        case KMeansGpuStatus::MissingDevice: return "MissingDevice";
        case KMeansGpuStatus::DeviceUnavailable: return "DeviceUnavailable";
        case KMeansGpuStatus::PlanningOnly: return "PlanningOnly";
        case KMeansGpuStatus::SizeOverflow: return "SizeOverflow";
        }
        return "Unknown";
    }

    const char* DebugNameForKMeansGpuPassKind(const KMeansGpuPassKind kind) noexcept
    {
        switch (kind)
        {
        case KMeansGpuPassKind::Reset: return "Reset";
        case KMeansGpuPassKind::Assign: return "Assign";
        case KMeansGpuPassKind::Update: return "Update";
        }
        return "Unknown";
    }

    const char* DebugNameForKMeansGpuBufferRole(const KMeansGpuBufferRole role) noexcept
    {
        switch (role)
        {
        case KMeansGpuBufferRole::None: return "None";
        case KMeansGpuBufferRole::State: return "State";
        case KMeansGpuBufferRole::PositionX: return "PositionX";
        case KMeansGpuBufferRole::PositionY: return "PositionY";
        case KMeansGpuBufferRole::PositionZ: return "PositionZ";
        case KMeansGpuBufferRole::Centroids: return "Centroids";
        case KMeansGpuBufferRole::NextCentroids: return "NextCentroids";
        case KMeansGpuBufferRole::SumX: return "SumX";
        case KMeansGpuBufferRole::SumY: return "SumY";
        case KMeansGpuBufferRole::SumZ: return "SumZ";
        case KMeansGpuBufferRole::Counts: return "Counts";
        case KMeansGpuBufferRole::Labels: return "Labels";
        case KMeansGpuBufferRole::SquaredDistances: return "SquaredDistances";
        case KMeansGpuBufferRole::Reduction: return "Reduction";
        case KMeansGpuBufferRole::LabelsReadback: return "LabelsReadback";
        case KMeansGpuBufferRole::SquaredDistancesReadback: return "SquaredDistancesReadback";
        case KMeansGpuBufferRole::CentroidsReadback: return "CentroidsReadback";
        }
        return "Unknown";
    }

    KMeansGpuBufferLayout ComputeKMeansGpuBufferLayout(const KMeansGpuPlanDesc& desc) noexcept
    {
        KMeansGpuBufferLayout layout{};

        const std::uint32_t n = desc.PointCount;
        const std::uint32_t k = std::min(desc.ClusterCount, desc.PointCount);
        if (n == 0u || desc.ClusterCount == 0u || k == 0u)
        {
            layout.Status = KMeansGpuStatus::InvalidInput;
            return layout;
        }

        layout.PointCount = n;
        layout.ClusterCount = k;

        constexpr std::uint64_t kFloatBytes = 4u;
        constexpr std::uint64_t kUIntBytes = 4u;
        const std::uint64_t nBytes = static_cast<std::uint64_t>(n) * kFloatBytes;
        const std::uint64_t kBytes = static_cast<std::uint64_t>(k) * kFloatBytes;
        const std::uint64_t centroidBytes = static_cast<std::uint64_t>(k) * 3u * kFloatBytes;
        const std::uint64_t countBytes = static_cast<std::uint64_t>(k) * kUIntBytes;
        const std::uint64_t labelBytes = static_cast<std::uint64_t>(n) * kUIntBytes;
        constexpr std::uint64_t kReductionBytes = 16u; // inertia, maxDist, argmax, changed

        std::uint64_t offset = 0u;
        const auto addSpan = [&](const KMeansGpuBufferRole role, const std::uint64_t size)
        {
            offset = AlignUp16(offset);
            layout.Spans.push_back(KMeansGpuBufferSpan{
                .Role = role, .OffsetBytes = offset, .SizeBytes = size});
            offset += size;
        };

        addSpan(KMeansGpuBufferRole::PositionX, nBytes);
        addSpan(KMeansGpuBufferRole::PositionY, nBytes);
        addSpan(KMeansGpuBufferRole::PositionZ, nBytes);
        addSpan(KMeansGpuBufferRole::Centroids, centroidBytes);
        addSpan(KMeansGpuBufferRole::NextCentroids, centroidBytes);
        addSpan(KMeansGpuBufferRole::SumX, kBytes);
        addSpan(KMeansGpuBufferRole::SumY, kBytes);
        addSpan(KMeansGpuBufferRole::SumZ, kBytes);
        addSpan(KMeansGpuBufferRole::Counts, countBytes);
        addSpan(KMeansGpuBufferRole::Labels, labelBytes);
        addSpan(KMeansGpuBufferRole::SquaredDistances, nBytes);
        addSpan(KMeansGpuBufferRole::Reduction, kReductionBytes);

        layout.WorkBufferBytes = AlignUp16(offset);
        if (layout.WorkBufferBytes > kMaxKMeansWorkBufferBytes)
        {
            layout.Status = KMeansGpuStatus::SizeOverflow;
            layout.Spans.clear();
            layout.WorkBufferBytes = 0u;
            return layout;
        }

        layout.StateBytes = sizeof(KMeansGpuStateBufferRecord);
        layout.LabelsReadbackBytes = labelBytes;
        layout.SquaredDistancesReadbackBytes = nBytes;
        layout.CentroidsReadbackBytes = centroidBytes;
        layout.Status = KMeansGpuStatus::Success;
        return layout;
    }

    KMeansGpuDispatchPlan ComputeKMeansGpuDispatchPlan(const KMeansGpuPlanDesc& desc)
    {
        KMeansGpuDispatchPlan plan{};

        const KMeansGpuBufferLayout layout = ComputeKMeansGpuBufferLayout(desc);
        plan.Layout = layout;
        if (!layout.IsValid())
        {
            plan.Status = layout.Status;
            return plan;
        }
        if (desc.MaxIterations == 0u)
        {
            plan.Status = KMeansGpuStatus::InvalidInput;
            return plan;
        }

        const std::uint32_t groupSize = desc.GroupSize == 0u ? kKMeansGpuGroupSize : desc.GroupSize;
        const std::uint32_t n = layout.PointCount;
        const std::uint32_t k = layout.ClusterCount;

        plan.PointCount = n;
        plan.ClusterCount = k;
        plan.MaxIterations = desc.MaxIterations;
        plan.GroupSize = groupSize;

        const std::uint32_t clusterGroups = CeilDiv(k, groupSize);
        const std::uint32_t pointGroups = CeilDiv(n, groupSize);

        plan.Dispatches.reserve(static_cast<std::size_t>(desc.MaxIterations) * 3u);
        for (std::uint32_t iteration = 0u; iteration < desc.MaxIterations; ++iteration)
        {
            plan.Dispatches.push_back(KMeansGpuDispatchDesc{
                .Kind = KMeansGpuPassKind::Reset, .Iteration = iteration,
                .ElementCount = k, .GroupSize = groupSize, .GroupCountX = clusterGroups});
            plan.Dispatches.push_back(KMeansGpuDispatchDesc{
                .Kind = KMeansGpuPassKind::Assign, .Iteration = iteration,
                .ElementCount = n, .GroupSize = groupSize, .GroupCountX = pointGroups});
            plan.Dispatches.push_back(KMeansGpuDispatchDesc{
                .Kind = KMeansGpuPassKind::Update, .Iteration = iteration,
                .ElementCount = k, .GroupSize = groupSize, .GroupCountX = clusterGroups});
        }

        plan.Status = KMeansGpuStatus::Success;
        return plan;
    }

    RHI::BufferDesc BuildKMeansGpuStateBufferDesc(const char* debugName) noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = sizeof(KMeansGpuStateBufferRecord),
            .Usage = RHI::BufferUsage::Storage |
                     RHI::BufferUsage::TransferSrc |
                     RHI::BufferUsage::TransferDst,
            .HostVisible = false,
            .DebugName = debugName,
        };
    }

    RHI::BufferDesc BuildKMeansGpuWorkBufferDesc(const KMeansGpuBufferLayout& layout,
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

    RHI::BufferDesc BuildKMeansGpuReadbackBufferDesc(const std::uint64_t sizeBytes,
                                                     const char* debugName) noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = sizeBytes,
            .Usage = RHI::BufferUsage::TransferDst | RHI::BufferUsage::TransferSrc,
            .HostVisible = true,
            .DebugName = debugName,
        };
    }

    RHI::PipelineDesc BuildKMeansResetPipelineDesc(const char* shaderPath)
    {
        return RHI::PipelineDesc{
            .ComputeShaderPath = shaderPath,
            .PushConstantSize = static_cast<std::uint32_t>(sizeof(KMeansGpuPassPushConstants)),
            .DebugName = "KMeansGpu.Reset",
        };
    }

    RHI::PipelineDesc BuildKMeansAssignPipelineDesc(const char* shaderPath)
    {
        return RHI::PipelineDesc{
            .ComputeShaderPath = shaderPath,
            .PushConstantSize = static_cast<std::uint32_t>(sizeof(KMeansGpuPassPushConstants)),
            .DebugName = "KMeansGpu.Assign",
        };
    }

    RHI::PipelineDesc BuildKMeansUpdatePipelineDesc(const char* shaderPath)
    {
        return RHI::PipelineDesc{
            .ComputeShaderPath = shaderPath,
            .PushConstantSize = static_cast<std::uint32_t>(sizeof(KMeansGpuPassPushConstants)),
            .DebugName = "KMeansGpu.Update",
        };
    }

    KMeansGpuResolveResult ResolveKMeansGpuRequest(const KMeansGpuResolveDesc& desc)
    {
        KMeansGpuResolveResult result{};
        result.Plan = ComputeKMeansGpuDispatchPlan(desc.Plan);

        if (!result.Plan.IsValid())
        {
            result.Status = result.Plan.Status;
            result.GpuExecutionAvailable = false;
            result.CpuFallbackRecommended = true;
            result.Diagnostic = "invalid k-means GPU plan";
            return result;
        }

        if (desc.Device == nullptr)
        {
            result.Status = KMeansGpuStatus::MissingDevice;
            result.GpuExecutionAvailable = false;
            result.CpuFallbackRecommended = true;
            result.Diagnostic = "no RHI device supplied for k-means GPU request";
            return result;
        }

        if (!desc.Device->IsOperational())
        {
            result.Status = KMeansGpuStatus::DeviceUnavailable;
            result.GpuExecutionAvailable = false;
            result.CpuFallbackRecommended = true;
            result.Diagnostic = "RHI device is not operational; k-means falls back to CPU";
            return result;
        }

        // GEOM-056 Slice A installs the planning core only; the recorded Lloyd
        // loop lands in later slices, so an operational device still resolves to
        // CPU fallback with honest telemetry.
        result.Status = KMeansGpuStatus::PlanningOnly;
        result.GpuExecutionAvailable = false;
        result.CpuFallbackRecommended = true;
        result.Diagnostic = "k-means GPU recording not implemented yet (GEOM-056 Slice A)";
        return result;
    }
}
