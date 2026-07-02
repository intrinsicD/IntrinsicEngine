module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.KMeansGpuBackend;

import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.Runtime.AsyncBufferReadback;

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

        [[nodiscard]] constexpr std::uint32_t CeilDiv(const std::uint32_t value,
                                                      const std::uint32_t divisor) noexcept
        {
            return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
        }

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

        [[nodiscard]] const KMeansGpuBufferSpan* FindSpan(
            const KMeansGpuBufferLayout& layout,
            const KMeansGpuBufferRole role) noexcept
        {
            const auto it = std::find_if(
                layout.Spans.begin(),
                layout.Spans.end(),
                [role](const KMeansGpuBufferSpan& span) noexcept
                {
                    return span.Role == role;
                });
            return it == layout.Spans.end() ? nullptr : &*it;
        }

        [[nodiscard]] KMeansGpuResourceCacheKey CacheKeyFor(
            const KMeansGpuBufferLayout& layout) noexcept
        {
            return KMeansGpuResourceCacheKey{
                .PointCount = layout.PointCount,
                .ClusterCount = layout.ClusterCount,
                .WorkBufferBytes = layout.WorkBufferBytes,
                .StateBytes = layout.StateBytes,
            };
        }

        [[nodiscard]] KMeansGpuExecutionResult ExecutionStatus(
            const KMeansGpuStatus status,
            const KMeansGpuPlanDesc& desc,
            const bool cpuFallbackRecommended = true)
        {
            return KMeansGpuExecutionResult{
                .Status = status,
                .Recorded = false,
                .CpuFallbackRecommended = cpuFallbackRecommended,
                .Plan = ComputeKMeansGpuDispatchPlan(desc),
            };
        }

        [[nodiscard]] KMeansGpuReadbackResult ReadbackStatus(
            const KMeansGpuStatus status,
            std::string diagnostic)
        {
            return KMeansGpuReadbackResult{
                .Status = status,
                .Read = false,
                .StructurallyValid = false,
                .CpuFallbackRecommended = true,
                .Diagnostic = std::move(diagnostic),
            };
        }

        [[nodiscard]] bool CreateOwnedBuffer(
            RHI::BufferManager& buffers,
            const RHI::BufferDesc& desc,
            RHI::BufferHandle& out,
            std::vector<RHI::BufferManager::BufferLease>& leases)
        {
            if (desc.SizeBytes == 0u)
                return false;

            auto leaseOr = buffers.Create(desc);
            if (!leaseOr.has_value())
                return false;

            RHI::BufferManager::BufferLease lease = std::move(leaseOr.value());
            out = lease.GetHandle();
            leases.push_back(std::move(lease));
            return true;
        }

        [[nodiscard]] KMeansGpuStatus AllocateExecutionResources(
            RHI::BufferManager& buffers,
            const KMeansGpuDispatchPlan& plan,
            KMeansGpuExecutionResources& out)
        {
            if (!plan.IsValid())
                return plan.Status;

            KMeansGpuExecutionResources resources{};
            resources.Layout = plan.Layout;
            resources.Key = CacheKeyFor(plan.Layout);

            if (!CreateOwnedBuffer(buffers,
                                   BuildKMeansGpuStateBufferDesc(),
                                   resources.Resources.State,
                                   resources.Leases) ||
                !CreateOwnedBuffer(buffers,
                                   BuildKMeansGpuWorkBufferDesc(plan.Layout),
                                   resources.Resources.Work,
                                   resources.Leases))
            {
                return KMeansGpuStatus::InvalidGpuResource;
            }

            out = std::move(resources);
            return KMeansGpuStatus::Success;
        }

        void UploadExecutionInputs(RHI::IDevice& device,
                                   const std::span<const glm::vec3> points,
                                   const std::span<const glm::vec3> seedCentroids,
                                   const KMeansGpuExecutionResources& resources,
                                   KMeansGpuExecutionResult& result)
        {
            const KMeansGpuBufferLayout& layout = resources.Layout;
            std::vector<float> x(points.size());
            std::vector<float> y(points.size());
            std::vector<float> z(points.size());
            for (std::size_t index = 0u; index < points.size(); ++index)
            {
                x[index] = points[index].x;
                y[index] = points[index].y;
                z[index] = points[index].z;
            }

            std::vector<float> packedCentroids(
                static_cast<std::size_t>(layout.ClusterCount) * 3u,
                0.0f);
            for (std::uint32_t cluster = 0u; cluster < layout.ClusterCount; ++cluster)
            {
                packedCentroids[static_cast<std::size_t>(cluster) * 3u + 0u] =
                    seedCentroids[cluster].x;
                packedCentroids[static_cast<std::size_t>(cluster) * 3u + 1u] =
                    seedCentroids[cluster].y;
                packedCentroids[static_cast<std::size_t>(cluster) * 3u + 2u] =
                    seedCentroids[cluster].z;
            }

            std::vector<std::uint32_t> initialLabels(points.size(), 0u);
            const KMeansGpuReductionRecord zeroReduction{};

            const auto writeRole =
                [&device, &resources, &layout, &result](
                    const KMeansGpuBufferRole role,
                    const void* data,
                    const std::uint64_t sizeBytes)
                {
                    const KMeansGpuBufferSpan* span = FindSpan(layout, role);
                    if (span == nullptr || sizeBytes == 0u || data == nullptr)
                        return;
                    device.WriteBuffer(resources.Resources.Work,
                                       data,
                                       sizeBytes,
                                       span->OffsetBytes);
                    ++result.UploadWriteCount;
                };

            writeRole(KMeansGpuBufferRole::PositionX, x.data(), FloatBytes(layout.PointCount));
            writeRole(KMeansGpuBufferRole::PositionY, y.data(), FloatBytes(layout.PointCount));
            writeRole(KMeansGpuBufferRole::PositionZ, z.data(), FloatBytes(layout.PointCount));
            writeRole(KMeansGpuBufferRole::Centroids,
                      packedCentroids.data(),
                      static_cast<std::uint64_t>(packedCentroids.size()) * sizeof(float));
            writeRole(KMeansGpuBufferRole::Labels,
                      initialLabels.data(),
                      Uint32Bytes(layout.PointCount));
            writeRole(KMeansGpuBufferRole::Reduction,
                      &zeroReduction,
                      sizeof(zeroReduction));
            result.UploadedInputs = true;
        }

        [[nodiscard]] AsyncBufferReadbackRequest MakeRoleReadbackRequest(
            const KMeansGpuExecutionResources& resources,
            const KMeansGpuBufferRole role)
        {
            const KMeansGpuBufferSpan* span = FindSpan(resources.Layout, role);
            if (span == nullptr)
                return {};

            return AsyncBufferReadbackRequest{
                .Source = resources.Resources.Work,
                .SourceDesc = BuildKMeansGpuWorkBufferDesc(resources.Layout),
                .SourceRange = RHI::BufferRange{
                    .OffsetBytes = span->OffsetBytes,
                    .SizeBytes = span->SizeBytes,
                },
                .SourceAccess = RHI::MemoryAccess::ShaderWrite,
            };
        }

        [[nodiscard]] std::vector<std::uint32_t> CopyUint32Readback(
            const std::span<const std::uint32_t> source)
        {
            return std::vector<std::uint32_t>{source.begin(), source.end()};
        }

        [[nodiscard]] std::vector<float> CopyFloatReadback(
            const std::span<const float> source)
        {
            return std::vector<float>{source.begin(), source.end()};
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
        case KMeansGpuStatus::ReadbackPending: return "ReadbackPending";
        case KMeansGpuStatus::InvalidReadback: return "InvalidReadback";
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

        const std::uint64_t nBytes = FloatBytes(n);
        const std::uint64_t kBytes = FloatBytes(k);
        const std::uint64_t centroidBytes = static_cast<std::uint64_t>(k) * 3u * sizeof(float);
        const std::uint64_t countBytes = Uint32Bytes(k);
        const std::uint64_t labelBytes = Uint32Bytes(n);
        constexpr std::uint64_t kReductionBytes = sizeof(KMeansGpuReductionRecord);

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
            .VertexShaderPath = {},
            .FragmentShaderPath = {},
            .ComputeShaderPath = shaderPath,
            .PushConstantSize = static_cast<std::uint32_t>(sizeof(KMeansGpuPassPushConstants)),
            .DebugName = "KMeansGpu.Reset",
        };
    }

    RHI::PipelineDesc BuildKMeansAssignPipelineDesc(const char* shaderPath)
    {
        return RHI::PipelineDesc{
            .VertexShaderPath = {},
            .FragmentShaderPath = {},
            .ComputeShaderPath = shaderPath,
            .PushConstantSize = static_cast<std::uint32_t>(sizeof(KMeansGpuPassPushConstants)),
            .DebugName = "KMeansGpu.Assign",
        };
    }

    RHI::PipelineDesc BuildKMeansUpdatePipelineDesc(const char* shaderPath)
    {
        return RHI::PipelineDesc{
            .VertexShaderPath = {},
            .FragmentShaderPath = {},
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

        result.Status = KMeansGpuStatus::Success;
        result.GpuExecutionAvailable = true;
        result.CpuFallbackRecommended = false;
        result.Diagnostic =
            "k-means GPU execution is available when the caller supplies pipelines, "
            "a command context, a persistent resource cache, and async readbacks";
        return result;
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

    KMeansGpuResourceCache::KMeansGpuResourceCache(RHI::BufferManager& buffers) noexcept
        : m_Buffers(&buffers)
    {
    }

    KMeansGpuResourceCache::~KMeansGpuResourceCache() = default;

    KMeansGpuStatus KMeansGpuResourceCache::Ensure(const KMeansGpuDispatchPlan& plan)
    {
        if (m_Buffers == nullptr)
            return KMeansGpuStatus::InvalidInput;
        if (!plan.IsValid())
            return plan.Status;

        const KMeansGpuResourceCacheKey key = CacheKeyFor(plan.Layout);
        if (m_Resources.IsValid() && m_Resources.Key == key)
            return KMeansGpuStatus::Success;

        KMeansGpuExecutionResources resources{};
        const KMeansGpuStatus status =
            AllocateExecutionResources(*m_Buffers, plan, resources);
        if (status != KMeansGpuStatus::Success)
            return status;

        m_Resources = std::move(resources);
        ++m_AllocationCount;
        return KMeansGpuStatus::Success;
    }

    void KMeansGpuResourceCache::Reset() noexcept
    {
        m_Resources = {};
    }

    const KMeansGpuExecutionResources* KMeansGpuResourceCache::Get() const noexcept
    {
        return m_Resources.IsValid() ? &m_Resources : nullptr;
    }

    KMeansGpuResourceCacheKey KMeansGpuResourceCache::Key() const noexcept
    {
        return m_Resources.Key;
    }

    std::uint32_t KMeansGpuResourceCache::AllocationCount() const noexcept
    {
        return m_AllocationCount;
    }

    KMeansGpuAsyncReadbacks::KMeansGpuAsyncReadbacks(
        Extrinsic::Graphics::GpuTransfer& transfer) noexcept
        : m_Labels(transfer)
        , m_SquaredDistances(transfer)
        , m_Centroids(transfer)
    {
    }

    bool KMeansGpuAsyncReadbacks::Enqueue(
        RHI::ICommandContext& cmd,
        const KMeansGpuExecutionResources& resources)
    {
        if (!resources.IsValid())
            return false;
        if (m_Labels.Status() == AsyncReadbackStatus::Pending ||
            m_SquaredDistances.Status() == AsyncReadbackStatus::Pending ||
            m_Centroids.Status() == AsyncReadbackStatus::Pending)
        {
            return false;
        }

        const AsyncBufferReadbackRequest labels =
            MakeRoleReadbackRequest(resources, KMeansGpuBufferRole::Labels);
        const AsyncBufferReadbackRequest distances =
            MakeRoleReadbackRequest(resources, KMeansGpuBufferRole::SquaredDistances);
        const AsyncBufferReadbackRequest centroids =
            MakeRoleReadbackRequest(resources, KMeansGpuBufferRole::Centroids);
        if (!labels.Source.IsValid() ||
            !distances.Source.IsValid() ||
            !centroids.Source.IsValid())
        {
            return false;
        }

        const bool labelsQueued = m_Labels.Enqueue(cmd, labels);
        const bool distancesQueued = m_SquaredDistances.Enqueue(cmd, distances);
        const bool centroidsQueued = m_Centroids.Enqueue(cmd, centroids);
        return labelsQueued && distancesQueued && centroidsQueued;
    }

    bool KMeansGpuAsyncReadbacks::Poll() noexcept
    {
        const bool labels = m_Labels.Poll();
        const bool distances = m_SquaredDistances.Poll();
        const bool centroids = m_Centroids.Poll();
        return labels && distances && centroids;
    }

    void KMeansGpuAsyncReadbacks::Reset() noexcept
    {
        m_Labels.Reset();
        m_SquaredDistances.Reset();
        m_Centroids.Reset();
    }

    bool KMeansGpuAsyncReadbacks::IsReady() const noexcept
    {
        return m_Labels.IsReady() &&
               m_SquaredDistances.IsReady() &&
               m_Centroids.IsReady();
    }

    KMeansGpuReadbackResult KMeansGpuAsyncReadbacks::Collect(
        const KMeansGpuDispatchPlan& plan) const
    {
        if (!plan.IsValid())
        {
            return ReadbackStatus(plan.Status,
                                  "k-means GPU readback requires a valid dispatch plan");
        }
        if (!IsReady())
        {
            return ReadbackStatus(KMeansGpuStatus::ReadbackPending,
                                  "k-means GPU readback is not ready yet");
        }

        const std::span<const std::uint32_t> labelSpan =
            m_Labels.BytesAs<std::uint32_t>();
        const std::span<const float> distanceSpan =
            m_SquaredDistances.BytesAs<float>();
        const std::span<const float> centroidSpan =
            m_Centroids.BytesAs<float>();

        if (labelSpan.size() != plan.PointCount ||
            distanceSpan.size() != plan.PointCount ||
            centroidSpan.size() != static_cast<std::size_t>(plan.ClusterCount) * 3u)
        {
            return ReadbackStatus(KMeansGpuStatus::InvalidReadback,
                                  "k-means GPU readback byte sizes do not match the dispatch plan");
        }

        KMeansGpuReadbackResult result{};
        result.Status = KMeansGpuStatus::Success;
        result.Read = true;
        result.StructurallyValid = true;
        result.CpuFallbackRecommended = false;
        result.Labels = CopyUint32Readback(labelSpan);
        result.SquaredDistances = CopyFloatReadback(distanceSpan);
        result.Centroids.resize(plan.ClusterCount);

        for (std::uint32_t cluster = 0u; cluster < plan.ClusterCount; ++cluster)
        {
            result.Centroids[cluster] = glm::vec3{
                centroidSpan[static_cast<std::size_t>(cluster) * 3u + 0u],
                centroidSpan[static_cast<std::size_t>(cluster) * 3u + 1u],
                centroidSpan[static_cast<std::size_t>(cluster) * 3u + 2u],
            };
        }

        for (std::uint32_t index = 0u; index < plan.PointCount; ++index)
        {
            if (result.Labels[index] >= plan.ClusterCount)
            {
                return ReadbackStatus(KMeansGpuStatus::InvalidReadback,
                                      "k-means GPU readback contains an out-of-range label");
            }

            const float distance = result.SquaredDistances[index];
            if (!std::isfinite(distance) || distance < 0.0f)
            {
                return ReadbackStatus(KMeansGpuStatus::InvalidReadback,
                                      "k-means GPU readback contains an invalid squared distance");
            }

            result.Inertia += distance;
            if (index == 0u || distance > result.SquaredDistances[result.MaxDistanceIndex])
            {
                result.MaxDistanceIndex = index;
            }
        }

        for (const glm::vec3& centroid : result.Centroids)
        {
            if (!std::isfinite(centroid.x) ||
                !std::isfinite(centroid.y) ||
                !std::isfinite(centroid.z))
            {
                return ReadbackStatus(KMeansGpuStatus::InvalidReadback,
                                      "k-means GPU readback contains an invalid centroid");
            }
        }

        return result;
    }

    KMeansGpuExecutionResult RecordKMeansGpuExecution(
        const KMeansGpuExecutionDesc& desc)
    {
        if (desc.Device == nullptr)
        {
            return ExecutionStatus(KMeansGpuStatus::MissingDevice, desc.Plan);
        }
        if (!desc.Device->IsOperational())
        {
            return ExecutionStatus(KMeansGpuStatus::DeviceUnavailable, desc.Plan);
        }
        if (desc.CommandContext == nullptr ||
            desc.ResourceCache == nullptr)
        {
            return ExecutionStatus(KMeansGpuStatus::InvalidInput, desc.Plan);
        }
        if (desc.Points.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
        {
            return ExecutionStatus(KMeansGpuStatus::SizeOverflow, desc.Plan);
        }
        if (desc.Plan.PointCount != static_cast<std::uint32_t>(desc.Points.size()))
        {
            return ExecutionStatus(KMeansGpuStatus::InvalidInput, desc.Plan);
        }

        KMeansGpuDispatchPlan plan = ComputeKMeansGpuDispatchPlan(desc.Plan);
        if (!plan.IsValid())
        {
            return KMeansGpuExecutionResult{
                .Status = plan.Status,
                .Recorded = false,
                .CpuFallbackRecommended = true,
                .Plan = std::move(plan),
            };
        }
        if (desc.SeedCentroids.size() < plan.ClusterCount)
        {
            return KMeansGpuExecutionResult{
                .Status = KMeansGpuStatus::InvalidInput,
                .Recorded = false,
                .CpuFallbackRecommended = true,
                .Plan = std::move(plan),
            };
        }

        KMeansGpuExecutionResult result{};
        result.Status = KMeansGpuStatus::Success;
        result.CpuFallbackRecommended = true;
        result.Plan = plan;

        const KMeansGpuStatus cacheStatus = desc.ResourceCache->Ensure(plan);
        if (cacheStatus != KMeansGpuStatus::Success)
        {
            result.Status = cacheStatus;
            return result;
        }

        const KMeansGpuExecutionResources* resources = desc.ResourceCache->Get();
        if (resources == nullptr || !resources->IsValid())
        {
            result.Status = KMeansGpuStatus::InvalidGpuResource;
            return result;
        }
        result.Resources = resources;

        UploadExecutionInputs(*desc.Device,
                              desc.Points,
                              desc.SeedCentroids,
                              *resources,
                              result);

        result.Record = RecordKMeansGpuPasses(KMeansGpuRecordDesc{
            .Device = desc.Device,
            .CommandContext = desc.CommandContext,
            .Pipelines = desc.Pipelines,
            .Resources = resources->Resources,
            .Plan = desc.Plan,
        });
        if (!result.Record.Succeeded() || !result.Record.Recorded)
        {
            result.Status = result.Record.Status;
            result.CpuFallbackRecommended = true;
            return result;
        }

        result.Recorded = true;
        result.ReadbackResourcesReady = true;
        result.CpuFallbackRecommended = false;
        return result;
    }
}
