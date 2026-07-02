module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

module Extrinsic.Runtime.KMeansGpuBackend;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;

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
        case KMeansGpuStatus::InvalidGpuResource: return "InvalidGpuResource";
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

    namespace
    {
        [[nodiscard]] std::uint64_t SpanOffset(const KMeansGpuBufferLayout& layout,
                                               const KMeansGpuBufferRole role) noexcept
        {
            for (const KMeansGpuBufferSpan& span : layout.Spans)
            {
                if (span.Role == role)
                    return span.OffsetBytes;
            }
            return 0u;
        }
    }

    KMeansGpuStateBufferRecord BuildKMeansGpuStateRecord(
        RHI::IDevice& device,
        const KMeansGpuResourceSet& resources,
        const KMeansGpuBufferLayout& layout) noexcept
    {
        KMeansGpuStateBufferRecord record{};
        if (!layout.IsValid() || !resources.Work.IsValid())
            return record;

        const std::uint64_t base = device.GetBufferDeviceAddress(resources.Work);
        if (base == 0u)
            return record; // BDA unsupported: shaders treat zero pointers as skip.

        const auto address = [&](const KMeansGpuBufferRole role)
        {
            return base + SpanOffset(layout, role);
        };

        record.PositionXBDA = address(KMeansGpuBufferRole::PositionX);
        record.PositionYBDA = address(KMeansGpuBufferRole::PositionY);
        record.PositionZBDA = address(KMeansGpuBufferRole::PositionZ);
        record.CentroidsBDA = address(KMeansGpuBufferRole::Centroids);
        record.NextCentroidsBDA = address(KMeansGpuBufferRole::NextCentroids);
        record.SumXBDA = address(KMeansGpuBufferRole::SumX);
        record.SumYBDA = address(KMeansGpuBufferRole::SumY);
        record.SumZBDA = address(KMeansGpuBufferRole::SumZ);
        record.CountsBDA = address(KMeansGpuBufferRole::Counts);
        record.LabelsBDA = address(KMeansGpuBufferRole::Labels);
        record.SquaredDistancesBDA = address(KMeansGpuBufferRole::SquaredDistances);
        record.ReductionBDA = address(KMeansGpuBufferRole::Reduction);
        return record;
    }

    KMeansGpuRecordResult RecordKMeansGpuPasses(const KMeansGpuRecordDesc& desc)
    {
        KMeansGpuRecordResult result{};
        result.CpuFallbackRecommended = true;

        if (desc.Device == nullptr)
        {
            result.Status = KMeansGpuStatus::MissingDevice;
            return result;
        }
        if (desc.CommandContext == nullptr)
        {
            result.Status = KMeansGpuStatus::InvalidInput;
            return result;
        }
        if (!desc.Device->IsOperational())
        {
            result.Status = KMeansGpuStatus::DeviceUnavailable;
            return result;
        }

        result.Plan = ComputeKMeansGpuDispatchPlan(desc.Plan);
        if (!result.Plan.IsValid())
        {
            result.Status = result.Plan.Status;
            return result;
        }

        if (!desc.Resources.IsValid() || !desc.Pipelines.IsValid())
        {
            result.Status = KMeansGpuStatus::InvalidGpuResource;
            return result;
        }

        RHI::IDevice& device = *desc.Device;
        RHI::ICommandContext& cmd = *desc.CommandContext;

        // Publish the BDA pointer table into the State buffer, then make it
        // visible to the compute passes.
        const KMeansGpuStateBufferRecord stateRecord =
            BuildKMeansGpuStateRecord(device, desc.Resources, result.Plan.Layout);
        device.WriteBuffer(desc.Resources.State, &stateRecord, sizeof(stateRecord), 0u);
        result.StateRecordUploaded = true;
        cmd.BufferBarrier(desc.Resources.State,
                          RHI::MemoryAccess::TransferWrite,
                          RHI::MemoryAccess::ShaderRead);

        const std::uint64_t stateBDA = device.GetBufferDeviceAddress(desc.Resources.State);
        const float tol = desc.Plan.ConvergenceTolerance;
        const float tolSquared = tol * tol;

        for (const KMeansGpuDispatchDesc& dispatch : result.Plan.Dispatches)
        {
            RHI::PipelineHandle pipeline{};
            switch (dispatch.Kind)
            {
            case KMeansGpuPassKind::Reset: pipeline = desc.Pipelines.Reset; break;
            case KMeansGpuPassKind::Assign: pipeline = desc.Pipelines.Assign; break;
            case KMeansGpuPassKind::Update: pipeline = desc.Pipelines.Update; break;
            }

            const KMeansGpuPassPushConstants push{
                .StateBDA = stateBDA,
                .PointCount = result.Plan.PointCount,
                .ClusterCount = result.Plan.ClusterCount,
                .GroupSize = result.Plan.GroupSize,
                .Iteration = dispatch.Iteration,
                .ConvergenceTolSquared = tolSquared,
                .Reserved0 = 0.0f,
            };

            cmd.BindPipeline(pipeline);
            cmd.PushConstants(&push, static_cast<std::uint32_t>(sizeof(push)), 0u);
            cmd.Dispatch(dispatch.GroupCountX, dispatch.GroupCountY, dispatch.GroupCountZ);
            ++result.MethodDispatchCount;

            // Every role lives in the packed Work buffer, so one barrier orders
            // this pass's writes before the next pass's reads/writes (and the
            // final barrier before any downstream readback copy).
            cmd.BufferBarrier(desc.Resources.Work,
                              RHI::MemoryAccess::ShaderWrite,
                              RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
        }

        result.Recorded = true;
        result.CpuFallbackRecommended = false;
        result.Status = KMeansGpuStatus::Success;
        return result;
    }
}
