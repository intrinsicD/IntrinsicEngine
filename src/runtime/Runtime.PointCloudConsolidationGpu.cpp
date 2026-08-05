module;

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.PointCloudConsolidationModule;

import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Geometry.PointCloud.Consolidation;
import Geometry.SupportRadius;

#include "internal/Runtime.PointCloudConsolidationGpu.Internal.hpp"

namespace Extrinsic::Runtime
{
    namespace Consolidation = Geometry::PointCloud::Consolidation;

    namespace
    {
        constexpr std::uint32_t kLopGpuGroupSize = 256u;
        constexpr std::uint32_t kMaximumLopGpuCells = 1'048'576u;
        constexpr std::uint64_t kMaximumLopGpuBytes =
            std::uint64_t{1u} << 30u;
        constexpr std::string_view kLopGpuImplementation =
            "gpu_vulkan_compute";

        enum class LopGpuAlgorithmError : std::uint32_t
        {
            None = 0u,
            Grid,
            EmptyDensity,
            EmptyAttraction,
            NonFinite,
            NeighborLimit,
            NotConverged,
        };

        struct LopGpuStateBufferRecord
        {
            std::uint64_t SourcePositionsBDA{0u};
            std::uint64_t SourceWeightsBDA{0u};
            std::uint64_t ProjectedABDA{0u};
            std::uint64_t ProjectedBBDA{0u};
            std::uint64_t ProjectedWeightsBDA{0u};
            std::uint64_t DisplacementsBDA{0u};
            std::uint64_t SourceCountsBDA{0u};
            std::uint64_t SourceOffsetsBDA{0u};
            std::uint64_t SourceCursorsBDA{0u};
            std::uint64_t SourceIndicesBDA{0u};
            std::uint64_t ProjectedCountsBDA{0u};
            std::uint64_t ProjectedOffsetsBDA{0u};
            std::uint64_t ProjectedCursorsBDA{0u};
            std::uint64_t ProjectedIndicesBDA{0u};
            std::uint64_t DiagnosticsBDA{0u};
        };
        static_assert(sizeof(LopGpuStateBufferRecord) == 120u);

        struct LopGpuPushConstants
        {
            std::uint64_t StateBDA{0u};
            std::uint32_t SourceCount{0u};
            std::uint32_t TargetCount{0u};
            std::uint32_t CellCount{0u};
            std::uint32_t DimX{0u};
            std::uint32_t DimY{0u};
            std::uint32_t DimZ{0u};
            std::uint32_t Iteration{0u};
            std::uint32_t MaxIterations{0u};
            std::uint32_t Strategy{0u};
            std::uint32_t PassMode{0u};
            std::uint32_t MaxNeighbors{0u};
            std::uint32_t Reserved0{0u};
            float OriginX{0.0f};
            float OriginY{0.0f};
            float OriginZ{0.0f};
            float SupportRadius{0.0f};
            float RepulsionWeight{0.0f};
            float ConvergenceTolerance{0.0f};
        };
        static_assert(sizeof(LopGpuPushConstants) == 80u);

        struct LopGpuDiagnosticsRecord
        {
            std::uint32_t ErrorCode{0u};
            std::uint32_t Iterations{0u};
            std::uint32_t Converged{0u};
            std::uint32_t Active{1u};
            std::uint32_t MaxDisplacementBits{0u};
            std::uint32_t DensityContributionCount{0u};
            std::uint32_t AttractionContributionCount{0u};
            std::uint32_t RepulsionContributionCount{0u};
            std::uint32_t EmptyNeighborhoodCount{0u};
            std::uint32_t SourceGridBuilt{0u};
            std::uint32_t ProjectedGridBuilds{0u};
            std::uint32_t Reserved0{0u};
            float AverageDisplacement{0.0f};
            float Reserved1{0.0f};
            float Reserved2{0.0f};
            float Reserved3{0.0f};
        };
        static_assert(sizeof(LopGpuDiagnosticsRecord) == 64u);

        struct LopGpuPlan
        {
            std::uint32_t SourceCount{0u};
            std::uint32_t TargetCount{0u};
            std::uint32_t CellCount{0u};
            std::uint32_t DimX{0u};
            std::uint32_t DimY{0u};
            std::uint32_t DimZ{0u};
            glm::vec3 Origin{0.0f};
            float SupportRadius{0.0f};
            bool DensityWeighted{false};
            Graphics::ParallelPrimitiveDispatchPlan ScanPlan{};
            std::uint64_t TotalBufferBytes{0u};

            [[nodiscard]] bool IsValid() const noexcept
            {
                return SourceCount > 0u && TargetCount > 0u &&
                    CellCount > 0u && DimX > 0u && DimY > 0u &&
                    DimZ > 0u &&
                    std::isfinite(Origin.x) &&
                    std::isfinite(Origin.y) &&
                    std::isfinite(Origin.z) &&
                    std::isfinite(SupportRadius) &&
                    SupportRadius > 0.0f && ScanPlan.IsValid() &&
                    TotalBufferBytes > 0u &&
                    TotalBufferBytes <= kMaximumLopGpuBytes;
            }
        };

        struct LopGpuPipelineSet
        {
            RHI::PipelineHandle GridCount{};
            RHI::PipelineHandle GridScatter{};
            RHI::PipelineHandle Density{};
            RHI::PipelineHandle Initialize{};
            RHI::PipelineHandle IterationReset{};
            RHI::PipelineHandle Project{};
            RHI::PipelineHandle IterationFinalize{};
            RHI::PipelineHandle FinalReduce{};
            Graphics::ParallelPrimitivePipelineSet Parallel{};

            [[nodiscard]] bool IsValid() const noexcept
            {
                return GridCount.IsValid() && GridScatter.IsValid() &&
                    Density.IsValid() && Initialize.IsValid() &&
                    IterationReset.IsValid() && Project.IsValid() &&
                    IterationFinalize.IsValid() && FinalReduce.IsValid() &&
                    Parallel.PrefixScan.IsValid() &&
                    Parallel.AddBlockOffsets.IsValid();
            }
        };

        struct LopGpuResources
        {
            RHI::BufferHandle State{};
            RHI::BufferHandle SourcePositions{};
            RHI::BufferHandle SourceWeights{};
            RHI::BufferHandle ProjectedA{};
            RHI::BufferHandle ProjectedB{};
            RHI::BufferHandle ProjectedWeights{};
            RHI::BufferHandle Displacements{};
            RHI::BufferHandle SourceCounts{};
            RHI::BufferHandle SourceOffsets{};
            RHI::BufferHandle SourceCursors{};
            RHI::BufferHandle SourceIndices{};
            RHI::BufferHandle ProjectedCounts{};
            RHI::BufferHandle ProjectedOffsets{};
            RHI::BufferHandle ProjectedCursors{};
            RHI::BufferHandle ProjectedIndices{};
            RHI::BufferHandle Diagnostics{};
            RHI::BufferHandle ScanScratch{};
            std::vector<RHI::BufferManager::BufferLease> Leases{};

            [[nodiscard]] bool IsValid() const noexcept
            {
                return State.IsValid() && SourcePositions.IsValid() &&
                    SourceWeights.IsValid() && ProjectedA.IsValid() &&
                    ProjectedB.IsValid() &&
                    ProjectedWeights.IsValid() &&
                    Displacements.IsValid() && SourceCounts.IsValid() &&
                    SourceOffsets.IsValid() && SourceCursors.IsValid() &&
                    SourceIndices.IsValid() && ProjectedCounts.IsValid() &&
                    ProjectedOffsets.IsValid() &&
                    ProjectedCursors.IsValid() &&
                    ProjectedIndices.IsValid() && Diagnostics.IsValid();
            }
        };

        class NoopCommandContext final : public RHI::ICommandContext
        {
        public:
            void Begin() override {}
            void End() override {}
            void BeginRenderPass(const RHI::RenderPassDesc&) override {}
            void EndRenderPass() override {}
            void SetViewport(float, float, float, float, float, float) override {}
            void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
            void BindPipeline(RHI::PipelineHandle) override {}
            void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, RHI::IndexType) override {}
            void PushConstants(const void*, std::uint32_t, std::uint32_t) override {}
            void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
            void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
            void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
            void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
            void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
            void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
            void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
            void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
            void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
            void BufferBarrier(RHI::BufferHandle, RHI::MemoryAccess, RHI::MemoryAccess) override {}
            void SubmitBarriers(const RHI::BarrierBatchDesc&) override {}
            void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
            void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
            void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
        };

        [[nodiscard]] constexpr std::uint32_t CeilDiv(
            const std::uint32_t value,
            const std::uint32_t divisor) noexcept
        {
            return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
        }

        [[nodiscard]] bool CheckedAdd(
            std::uint64_t& total,
            const std::uint64_t value) noexcept
        {
            if (value > std::numeric_limits<std::uint64_t>::max() - total)
                return false;
            total += value;
            return total <= kMaximumLopGpuBytes;
        }

        [[nodiscard]] std::optional<LopGpuPlan> BuildPlan(
            const PointCloudConsolidationSnapshot& snapshot)
        {
            if (!snapshot.RadiusAnalysis.has_value() ||
                !snapshot.RadiusAnalysis->Succeeded() ||
                snapshot.Positions.empty() ||
                snapshot.GpuInitialPositions.empty() ||
                snapshot.Positions.size() >
                    std::numeric_limits<std::uint32_t>::max() ||
                snapshot.GpuInitialPositions.size() >
                    std::numeric_limits<std::uint32_t>::max())
            {
                return std::nullopt;
            }

            const bool lop = std::holds_alternative<
                Consolidation::LopStrategy>(snapshot.Params.Method);
            const auto* const wlop = std::get_if<
                Consolidation::WlopStrategy>(&snapshot.Params.Method);
            if (!lop &&
                (wlop == nullptr ||
                 wlop->Weighting !=
                     Consolidation::WeightingMode::Isotropic))
            {
                return std::nullopt;
            }

            float support = static_cast<float>(
                snapshot.Params.SupportRadius);
            if (static_cast<double>(support) <
                snapshot.Params.SupportRadius)
            {
                support = std::nextafter(
                    support,
                    std::numeric_limits<float>::infinity());
            }
            if (!std::isfinite(support) || !(support > 0.0f))
                return std::nullopt;

            glm::vec3 minimum = snapshot.Positions.front();
            glm::vec3 maximum = minimum;
            for (const glm::vec3 point : snapshot.Positions)
            {
                minimum = glm::min(minimum, point);
                maximum = glm::max(maximum, point);
            }
            const glm::vec3 origin = glm::vec3{
                std::nextafter(
                    minimum.x - 2.0f * support,
                    -std::numeric_limits<float>::infinity()),
                std::nextafter(
                    minimum.y - 2.0f * support,
                    -std::numeric_limits<float>::infinity()),
                std::nextafter(
                    minimum.z - 2.0f * support,
                    -std::numeric_limits<float>::infinity()),
            };

            const auto dimension = [support](const float extent)
                -> std::optional<std::uint32_t>
            {
                const double cells = std::ceil(
                    static_cast<double>(extent) /
                    static_cast<double>(support)) + 5.0;
                if (!std::isfinite(cells) || cells < 5.0 ||
                    cells > static_cast<double>(
                        std::numeric_limits<std::uint32_t>::max()))
                {
                    return std::nullopt;
                }
                return static_cast<std::uint32_t>(cells);
            };
            const auto dimX = dimension(maximum.x - minimum.x);
            const auto dimY = dimension(maximum.y - minimum.y);
            const auto dimZ = dimension(maximum.z - minimum.z);
            if (!dimX.has_value() || !dimY.has_value() ||
                !dimZ.has_value())
            {
                return std::nullopt;
            }
            const std::uint64_t cellCount64 =
                static_cast<std::uint64_t>(*dimX) * *dimY * *dimZ;
            if (cellCount64 == 0u ||
                cellCount64 > kMaximumLopGpuCells)
            {
                return std::nullopt;
            }

            const std::uint64_t sourceCount = snapshot.Positions.size();
            const std::uint64_t targetCount =
                snapshot.GpuInitialPositions.size();
            LopGpuPlan plan{
                .SourceCount = static_cast<std::uint32_t>(sourceCount),
                .TargetCount = static_cast<std::uint32_t>(targetCount),
                .CellCount = static_cast<std::uint32_t>(cellCount64),
                .DimX = *dimX,
                .DimY = *dimY,
                .DimZ = *dimZ,
                .Origin = origin,
                .SupportRadius = support,
                .DensityWeighted = wlop != nullptr,
                .ScanPlan = Graphics::ComputePrefixScanDispatchPlan(
                    static_cast<std::uint32_t>(cellCount64),
                    Graphics::PrefixScanMode::Exclusive),
            };

            const std::uint64_t vec4Source = sourceCount * sizeof(glm::vec4);
            const std::uint64_t vec4Target = targetCount * sizeof(glm::vec4);
            const std::uint64_t floatSource = sourceCount * sizeof(float);
            const std::uint64_t floatTarget = targetCount * sizeof(float);
            const std::uint64_t uintCells = cellCount64 * sizeof(std::uint32_t);
            const std::uint64_t uintSource = sourceCount * sizeof(std::uint32_t);
            const std::uint64_t uintTarget = targetCount * sizeof(std::uint32_t);
            std::uint64_t total = 0u;
            const bool sizeValid =
                CheckedAdd(total, sizeof(LopGpuStateBufferRecord)) &&
                CheckedAdd(total, vec4Source) &&
                CheckedAdd(total, floatSource) &&
                CheckedAdd(total, vec4Target * 2u) &&
                CheckedAdd(total, floatTarget * 2u) &&
                CheckedAdd(total, uintCells * 6u) &&
                CheckedAdd(total, uintSource) &&
                CheckedAdd(total, uintTarget) &&
                CheckedAdd(total, sizeof(LopGpuDiagnosticsRecord)) &&
                CheckedAdd(total, plan.ScanPlan.ScratchBytes);
            if (!sizeValid)
                return std::nullopt;
            plan.TotalBufferBytes = total;
            return plan.IsValid()
                ? std::optional<LopGpuPlan>{std::move(plan)}
                : std::nullopt;
        }

        [[nodiscard]] RHI::BufferDesc StorageBufferDesc(
            const std::uint64_t sizeBytes,
            const char* debugName) noexcept
        {
            return RHI::BufferDesc{
                .SizeBytes = sizeBytes,
                .Usage = RHI::BufferUsage::Storage |
                    RHI::BufferUsage::TransferSrc |
                    RHI::BufferUsage::TransferDst,
                .HostVisible = false,
                .DebugName = debugName,
            };
        }

        [[nodiscard]] bool CreateBuffer(
            RHI::BufferManager& buffers,
            const RHI::BufferDesc& desc,
            RHI::BufferHandle& handle,
            std::vector<RHI::BufferManager::BufferLease>& leases)
        {
            if (desc.SizeBytes == 0u)
                return false;
            auto leaseOr = buffers.Create(desc);
            if (!leaseOr.has_value())
                return false;
            RHI::BufferManager::BufferLease lease =
                std::move(*leaseOr);
            handle = lease.GetHandle();
            leases.push_back(std::move(lease));
            return true;
        }

        [[nodiscard]] bool AllocateResources(
            RHI::BufferManager& buffers,
            const LopGpuPlan& plan,
            LopGpuResources& resources)
        {
            const std::uint64_t sourceVec4Bytes =
                static_cast<std::uint64_t>(plan.SourceCount) *
                sizeof(glm::vec4);
            const std::uint64_t targetVec4Bytes =
                static_cast<std::uint64_t>(plan.TargetCount) *
                sizeof(glm::vec4);
            const std::uint64_t sourceFloatBytes =
                static_cast<std::uint64_t>(plan.SourceCount) *
                sizeof(float);
            const std::uint64_t targetFloatBytes =
                static_cast<std::uint64_t>(plan.TargetCount) *
                sizeof(float);
            const std::uint64_t cellBytes =
                static_cast<std::uint64_t>(plan.CellCount) *
                sizeof(std::uint32_t);
            const std::uint64_t sourceIndexBytes =
                static_cast<std::uint64_t>(plan.SourceCount) *
                sizeof(std::uint32_t);
            const std::uint64_t targetIndexBytes =
                static_cast<std::uint64_t>(plan.TargetCount) *
                sizeof(std::uint32_t);

            LopGpuResources allocated{};
            const bool created =
                CreateBuffer(
                    buffers,
                    StorageBufferDesc(
                        sizeof(LopGpuStateBufferRecord),
                        "LopGpu.State"),
                    allocated.State,
                    allocated.Leases) &&
                CreateBuffer(
                    buffers,
                    StorageBufferDesc(
                        sourceVec4Bytes, "LopGpu.SourcePositions"),
                    allocated.SourcePositions,
                    allocated.Leases) &&
                CreateBuffer(
                    buffers,
                    StorageBufferDesc(
                        sourceFloatBytes, "LopGpu.SourceWeights"),
                    allocated.SourceWeights,
                    allocated.Leases) &&
                CreateBuffer(
                    buffers,
                    StorageBufferDesc(targetVec4Bytes, "LopGpu.ProjectedA"),
                    allocated.ProjectedA,
                    allocated.Leases) &&
                CreateBuffer(
                    buffers,
                    StorageBufferDesc(targetVec4Bytes, "LopGpu.ProjectedB"),
                    allocated.ProjectedB,
                    allocated.Leases) &&
                CreateBuffer(
                    buffers,
                    StorageBufferDesc(
                        targetFloatBytes, "LopGpu.ProjectedWeights"),
                    allocated.ProjectedWeights,
                    allocated.Leases) &&
                CreateBuffer(
                    buffers,
                    StorageBufferDesc(
                        targetFloatBytes, "LopGpu.Displacements"),
                    allocated.Displacements,
                    allocated.Leases) &&
                CreateBuffer(buffers, StorageBufferDesc(cellBytes, "LopGpu.SourceCounts"), allocated.SourceCounts, allocated.Leases) &&
                CreateBuffer(buffers, StorageBufferDesc(cellBytes, "LopGpu.SourceOffsets"), allocated.SourceOffsets, allocated.Leases) &&
                CreateBuffer(buffers, StorageBufferDesc(cellBytes, "LopGpu.SourceCursors"), allocated.SourceCursors, allocated.Leases) &&
                CreateBuffer(buffers, StorageBufferDesc(sourceIndexBytes, "LopGpu.SourceIndices"), allocated.SourceIndices, allocated.Leases) &&
                CreateBuffer(buffers, StorageBufferDesc(cellBytes, "LopGpu.ProjectedCounts"), allocated.ProjectedCounts, allocated.Leases) &&
                CreateBuffer(buffers, StorageBufferDesc(cellBytes, "LopGpu.ProjectedOffsets"), allocated.ProjectedOffsets, allocated.Leases) &&
                CreateBuffer(buffers, StorageBufferDesc(cellBytes, "LopGpu.ProjectedCursors"), allocated.ProjectedCursors, allocated.Leases) &&
                CreateBuffer(buffers, StorageBufferDesc(targetIndexBytes, "LopGpu.ProjectedIndices"), allocated.ProjectedIndices, allocated.Leases) &&
                CreateBuffer(buffers, StorageBufferDesc(sizeof(LopGpuDiagnosticsRecord), "LopGpu.Diagnostics"), allocated.Diagnostics, allocated.Leases);
            if (!created)
                return false;
            if (plan.ScanPlan.ScratchBytes > 0u &&
                !CreateBuffer(
                    buffers,
                    Graphics::BuildParallelPrimitiveScratchBufferDesc(
                        plan.ScanPlan,
                        "LopGpu.ScanScratch"),
                    allocated.ScanScratch,
                    allocated.Leases))
            {
                return false;
            }
            if (!allocated.IsValid())
                return false;
            resources = std::move(allocated);
            return true;
        }

        [[nodiscard]] RHI::PipelineDesc MethodPipelineDesc(
            const char* shaderPath,
            const char* debugName)
        {
            return RHI::PipelineDesc{
                .VertexShaderPath = {},
                .FragmentShaderPath = {},
                .ComputeShaderPath = shaderPath,
                .PushConstantSize = static_cast<std::uint32_t>(
                    sizeof(LopGpuPushConstants)),
                .DebugName = debugName,
            };
        }

        [[nodiscard]] Consolidation::Status GeometryStatus(
            const LopGpuAlgorithmError error) noexcept
        {
            switch (error)
            {
            case LopGpuAlgorithmError::None:
                return Consolidation::Status::Success;
            case LopGpuAlgorithmError::Grid:
                return Consolidation::Status::SpatialQueryFailed;
            case LopGpuAlgorithmError::EmptyDensity:
            case LopGpuAlgorithmError::EmptyAttraction:
                return Consolidation::Status::EmptyNeighborhood;
            case LopGpuAlgorithmError::NonFinite:
                return Consolidation::Status::NumericalFailure;
            case LopGpuAlgorithmError::NeighborLimit:
                return Consolidation::Status::ResourceLimit;
            case LopGpuAlgorithmError::NotConverged:
                return Consolidation::Status::NotConverged;
            }
            return Consolidation::Status::NumericalFailure;
        }
    }

    struct PointCloudConsolidationGpuState::Impl
    {
        struct ActiveOperation
        {
            PointCloudConsolidationSnapshot Snapshot{};
            LopGpuPlan Plan{};
            LopGpuResources Resources{};
            std::uint64_t ProducerCompletedFrame{0u};
            bool ProducerSubmitted{false};
            bool ReadbackSubmitted{false};
            std::vector<Graphics::GpuTransferReadbackRangeDesc> Ranges{};
            Graphics::GpuTransferReadbackBatchTicket Ticket{};
        };

        Impl(
            RHI::IDevice& device,
            RHI::BufferManager& buffers,
            RHI::ITransferQueue& transferQueue)
            : Device(&device)
            , Buffers(&buffers)
            , Transfer(transferQueue)
        {
        }

        ~Impl()
        {
            if (Active.has_value() && Active->Ticket.IsValid())
                (void)Transfer.CancelReadbackBatch(Active->Ticket);
            DestroyPipelines();
        }

        [[nodiscard]] bool HasBusyState() const noexcept
        {
            return Active.has_value() || Completed.has_value();
        }

        [[nodiscard]] PointCloudConsolidationGpuSubmission Start(
            PointCloudConsolidationSnapshot& snapshot)
        {
            if (HasBusyState())
            {
                return PointCloudConsolidationGpuSubmission{
                    .Diagnostic =
                        "A point-cloud consolidation Vulkan operation is already pending.",
                };
            }
            if (!RetiredResources.empty())
            {
                return PointCloudConsolidationGpuSubmission{
                    .Diagnostic =
                        "A prior point-cloud consolidation Vulkan recording failure retained in-flight resources until shutdown; further requests use the CPU reference.",
                };
            }
            if (Device == nullptr || Buffers == nullptr ||
                !Device->IsOperational())
            {
                return PointCloudConsolidationGpuSubmission{
                    .Diagnostic =
                        "The RHI device is not operational for point-cloud consolidation Vulkan compute.",
                };
            }
            std::optional<LopGpuPlan> plan = BuildPlan(snapshot);
            if (!plan.has_value())
            {
                return PointCloudConsolidationGpuSubmission{
                    .Diagnostic =
                        "The point-cloud consolidation Vulkan grid/resource plan exceeded its bounded cell, counter, or memory contract.",
                };
            }
            Active = ActiveOperation{
                .Snapshot = std::move(snapshot),
                .Plan = std::move(*plan),
            };
            return PointCloudConsolidationGpuSubmission{.Accepted = true};
        }

        void RecordFrameCommands(RHI::ICommandContext& commandContext)
        {
            if (!Active.has_value() || Completed.has_value() ||
                Device == nullptr || Buffers == nullptr)
            {
                return;
            }
            if (!Active->ProducerSubmitted)
            {
                RecordProducerCommands(commandContext);
                return;
            }
            if (!Active->ReadbackSubmitted)
            {
                const std::uint32_t framesInFlight =
                    std::max(Device->GetFramesInFlight(), 1u);
                const std::uint64_t requiredFrame =
                    Active->ProducerCompletedFrame +
                    static_cast<std::uint64_t>(framesInFlight - 1u);
                if (Device->GetGlobalFrameNumber() < requiredFrame)
                    return;
                RecordReadbackCommands(commandContext);
            }
        }

        void DrainCompletedTransfers()
        {
            if (!Active.has_value() || !Active->ReadbackSubmitted)
                return;
            NoopCommandContext noop;
            Transfer.DrainCompleted(noop);
            if (Transfer.ReadbackBatchState(Active->Ticket) !=
                Graphics::GpuTransferReadbackBatchState::Ready)
            {
                return;
            }

            Graphics::GpuTransferReadbackBatchResult batch{};
            if (!Transfer.ConsumeReadbackBatch(
                    Active->Ticket,
                    Active->Ranges,
                    batch) ||
                !batch.IsValid())
            {
                CompleteFallback(
                    "Point-cloud consolidation Vulkan readback failed exact multi-range transport validation.");
                return;
            }
            CompleteReadback(batch);
        }

        [[nodiscard]] std::optional<PointCloudConsolidationGpuResult>
        ConsumeCompleted()
        {
            if (!Completed.has_value())
                return std::nullopt;
            auto result = std::move(Completed);
            Completed.reset();
            return result;
        }

        [[nodiscard]] bool EnsurePipelines()
        {
            if (Pipelines.IsValid())
                return true;
            if (Device == nullptr || !Device->IsOperational())
                return false;

            const auto shader = [](const char* name)
            {
                return Core::Filesystem::GetShaderPath(name);
            };
            const std::string gridCount =
                shader("shaders/lop_grid_count.comp.spv");
            const std::string gridScatter =
                shader("shaders/lop_grid_scatter.comp.spv");
            const std::string density =
                shader("shaders/lop_density.comp.spv");
            const std::string initialize =
                shader("shaders/lop_initialize.comp.spv");
            const std::string reset =
                shader("shaders/lop_iteration_reset.comp.spv");
            const std::string project =
                shader("shaders/lop_project.comp.spv");
            const std::string finalize =
                shader("shaders/lop_iteration_finalize.comp.spv");
            const std::string reduce =
                shader("shaders/lop_final_reduce.comp.spv");
            const std::string prefix =
                shader("shaders/parallel_prefix_scan.comp.spv");
            const std::string addOffsets =
                shader("shaders/parallel_scan_add_offsets.comp.spv");

            Pipelines.GridCount = Device->CreatePipeline(
                MethodPipelineDesc(
                    gridCount.c_str(), "LopGpu.GridCount"));
            Pipelines.GridScatter = Device->CreatePipeline(
                MethodPipelineDesc(
                    gridScatter.c_str(), "LopGpu.GridScatter"));
            Pipelines.Density = Device->CreatePipeline(
                MethodPipelineDesc(
                    density.c_str(), "LopGpu.Density"));
            Pipelines.Initialize = Device->CreatePipeline(
                MethodPipelineDesc(
                    initialize.c_str(), "LopGpu.Initialize"));
            Pipelines.IterationReset = Device->CreatePipeline(
                MethodPipelineDesc(
                    reset.c_str(), "LopGpu.IterationReset"));
            Pipelines.Project = Device->CreatePipeline(
                MethodPipelineDesc(
                    project.c_str(), "LopGpu.Project"));
            Pipelines.IterationFinalize = Device->CreatePipeline(
                MethodPipelineDesc(
                    finalize.c_str(), "LopGpu.IterationFinalize"));
            Pipelines.FinalReduce = Device->CreatePipeline(
                MethodPipelineDesc(
                    reduce.c_str(), "LopGpu.FinalReduce"));
            Pipelines.Parallel.PrefixScan = Device->CreatePipeline(
                Graphics::BuildParallelPrefixScanPipelineDesc(
                    prefix.c_str()));
            Pipelines.Parallel.AddBlockOffsets = Device->CreatePipeline(
                Graphics::BuildParallelScanAddOffsetsPipelineDesc(
                    addOffsets.c_str()));
            if (!Pipelines.IsValid())
            {
                DestroyPipelines();
                return false;
            }
            return true;
        }

        void DestroyPipelines()
        {
            if (Device == nullptr)
            {
                Pipelines = {};
                return;
            }
            const auto destroy = [this](RHI::PipelineHandle& pipeline)
            {
                if (pipeline.IsValid())
                    Device->DestroyPipeline(pipeline);
                pipeline = {};
            };
            destroy(Pipelines.GridCount);
            destroy(Pipelines.GridScatter);
            destroy(Pipelines.Density);
            destroy(Pipelines.Initialize);
            destroy(Pipelines.IterationReset);
            destroy(Pipelines.Project);
            destroy(Pipelines.IterationFinalize);
            destroy(Pipelines.FinalReduce);
            destroy(Pipelines.Parallel.PrefixScan);
            destroy(Pipelines.Parallel.AddBlockOffsets);
            Pipelines = {};
        }

        [[nodiscard]] LopGpuPushConstants Push(
            const std::uint32_t iteration,
            const std::uint32_t passMode) const
        {
            const ActiveOperation& active = *Active;
            const PointCloudConsolidationConfig& config =
                active.Snapshot.Request.Config;
            return LopGpuPushConstants{
                .StateBDA = Device->GetBufferDeviceAddress(
                    active.Resources.State),
                .SourceCount = active.Plan.SourceCount,
                .TargetCount = active.Plan.TargetCount,
                .CellCount = active.Plan.CellCount,
                .DimX = active.Plan.DimX,
                .DimY = active.Plan.DimY,
                .DimZ = active.Plan.DimZ,
                .Iteration = iteration,
                .MaxIterations = active.Snapshot.Params.MaxIterations,
                .Strategy = active.Plan.DensityWeighted ? 1u : 0u,
                .PassMode = passMode,
                .MaxNeighbors = config.MaxSupportNeighbors,
                .OriginX = active.Plan.Origin.x,
                .OriginY = active.Plan.Origin.y,
                .OriginZ = active.Plan.Origin.z,
                .SupportRadius = active.Plan.SupportRadius,
                .RepulsionWeight = static_cast<float>(
                    active.Snapshot.Params.RepulsionWeight),
                .ConvergenceTolerance = static_cast<float>(
                    active.Snapshot.Params.ConvergenceTolerance),
            };
        }

        void Dispatch(
            RHI::ICommandContext& commandContext,
            const RHI::PipelineHandle pipeline,
            const LopGpuPushConstants& push,
            const std::uint32_t elementCount)
        {
            commandContext.BindPipeline(pipeline);
            commandContext.PushConstants(
                &push,
                static_cast<std::uint32_t>(sizeof(push)),
                0u);
            commandContext.Dispatch(
                std::max(CeilDiv(elementCount, kLopGpuGroupSize), 1u),
                1u,
                1u);
        }

        [[nodiscard]] bool RecordGrid(
            RHI::ICommandContext& commandContext,
            const bool source,
            const std::uint32_t iteration)
        {
            ActiveOperation& active = *Active;
            LopGpuResources& resources = active.Resources;
            const RHI::BufferHandle counts = source
                ? resources.SourceCounts
                : resources.ProjectedCounts;
            const RHI::BufferHandle offsets = source
                ? resources.SourceOffsets
                : resources.ProjectedOffsets;
            const RHI::BufferHandle cursors = source
                ? resources.SourceCursors
                : resources.ProjectedCursors;
            const RHI::BufferHandle indices = source
                ? resources.SourceIndices
                : resources.ProjectedIndices;
            const std::uint64_t cellBytes =
                static_cast<std::uint64_t>(active.Plan.CellCount) *
                sizeof(std::uint32_t);
            if (!source && iteration != 0u)
            {
                // The projected grid is rebuilt every iteration. Its prior
                // shader reads/writes must be made available before the next
                // transfer-stage clear; the post-fill barriers below only
                // establish the opposite transfer -> compute direction.
                commandContext.BufferBarrier(
                    counts,
                    RHI::MemoryAccess::ShaderRead,
                    RHI::MemoryAccess::TransferWrite);
                commandContext.BufferBarrier(
                    cursors,
                    RHI::MemoryAccess::ShaderWrite,
                    RHI::MemoryAccess::TransferWrite);
            }
            commandContext.FillBuffer(counts, 0u, cellBytes, 0u);
            commandContext.FillBuffer(cursors, 0u, cellBytes, 0u);
            commandContext.BufferBarrier(
                counts,
                RHI::MemoryAccess::TransferWrite,
                RHI::MemoryAccess::ShaderRead |
                    RHI::MemoryAccess::ShaderWrite);
            commandContext.BufferBarrier(
                cursors,
                RHI::MemoryAccess::TransferWrite,
                RHI::MemoryAccess::ShaderRead |
                    RHI::MemoryAccess::ShaderWrite);

            const LopGpuPushConstants push = Push(
                iteration, source ? 0u : 1u);
            Dispatch(
                commandContext,
                Pipelines.GridCount,
                push,
                source ? active.Plan.SourceCount
                       : active.Plan.TargetCount);
            commandContext.BufferBarrier(
                counts,
                RHI::MemoryAccess::ShaderWrite,
                RHI::MemoryAccess::ShaderRead);

            Graphics::GpuParallelPrimitiveRecordResult scan =
                Graphics::RecordGpuPrefixScan(
                    Graphics::GpuPrefixScanRecordDesc{
                        .Device = Device,
                        .CommandContext = &commandContext,
                        .Buffers = Buffers,
                        .Pipelines = Pipelines.Parallel,
                        .Input = counts,
                        .Output = offsets,
                        .Scratch = resources.ScanScratch,
                        .ElementCount = active.Plan.CellCount,
                        .Mode = Graphics::PrefixScanMode::Exclusive,
                    });
            if (!scan.Succeeded() || !scan.Recorded)
                return false;

            Dispatch(
                commandContext,
                Pipelines.GridScatter,
                push,
                source ? active.Plan.SourceCount
                       : active.Plan.TargetCount);
            commandContext.BufferBarrier(
                indices,
                RHI::MemoryAccess::ShaderWrite,
                RHI::MemoryAccess::ShaderRead);
            return true;
        }

        void UploadInputs()
        {
            ActiveOperation& active = *Active;
            LopGpuResources& resources = active.Resources;
            std::vector<glm::vec4> source(active.Plan.SourceCount);
            for (std::uint32_t index = 0u;
                 index < active.Plan.SourceCount;
                 ++index)
            {
                source[index] = glm::vec4{
                    active.Snapshot.Positions[index], 1.0f};
            }
            std::vector<glm::vec4> projected(active.Plan.TargetCount);
            for (std::uint32_t index = 0u;
                 index < active.Plan.TargetCount;
                 ++index)
            {
                projected[index] = glm::vec4{
                    active.Snapshot.GpuInitialPositions[index], 1.0f};
            }
            const std::vector<float> sourceWeights(
                active.Plan.SourceCount, 1.0f);
            const std::vector<float> projectedWeights(
                active.Plan.TargetCount, 1.0f);
            const std::vector<float> displacements(
                active.Plan.TargetCount, 0.0f);
            const LopGpuDiagnosticsRecord diagnostics{
                .Active = 1u,
            };

            const LopGpuStateBufferRecord state{
                .SourcePositionsBDA = Device->GetBufferDeviceAddress(
                    resources.SourcePositions),
                .SourceWeightsBDA = Device->GetBufferDeviceAddress(
                    resources.SourceWeights),
                .ProjectedABDA = Device->GetBufferDeviceAddress(
                    resources.ProjectedA),
                .ProjectedBBDA = Device->GetBufferDeviceAddress(
                    resources.ProjectedB),
                .ProjectedWeightsBDA = Device->GetBufferDeviceAddress(
                    resources.ProjectedWeights),
                .DisplacementsBDA = Device->GetBufferDeviceAddress(
                    resources.Displacements),
                .SourceCountsBDA = Device->GetBufferDeviceAddress(
                    resources.SourceCounts),
                .SourceOffsetsBDA = Device->GetBufferDeviceAddress(
                    resources.SourceOffsets),
                .SourceCursorsBDA = Device->GetBufferDeviceAddress(
                    resources.SourceCursors),
                .SourceIndicesBDA = Device->GetBufferDeviceAddress(
                    resources.SourceIndices),
                .ProjectedCountsBDA = Device->GetBufferDeviceAddress(
                    resources.ProjectedCounts),
                .ProjectedOffsetsBDA = Device->GetBufferDeviceAddress(
                    resources.ProjectedOffsets),
                .ProjectedCursorsBDA = Device->GetBufferDeviceAddress(
                    resources.ProjectedCursors),
                .ProjectedIndicesBDA = Device->GetBufferDeviceAddress(
                    resources.ProjectedIndices),
                .DiagnosticsBDA = Device->GetBufferDeviceAddress(
                    resources.Diagnostics),
            };

            (void)Graphics::SubmitBufferUpload(
                *Device,
                resources.SourcePositions,
                source.data(),
                static_cast<std::uint64_t>(source.size()) *
                    sizeof(glm::vec4),
                0u);
            (void)Graphics::SubmitBufferUpload(
                *Device,
                resources.ProjectedB,
                projected.data(),
                static_cast<std::uint64_t>(projected.size()) *
                    sizeof(glm::vec4),
                0u);
            (void)Graphics::SubmitBufferUpload(
                *Device,
                resources.SourceWeights,
                sourceWeights.data(),
                static_cast<std::uint64_t>(sourceWeights.size()) *
                    sizeof(float),
                0u);
            (void)Graphics::SubmitBufferUpload(
                *Device,
                resources.ProjectedWeights,
                projectedWeights.data(),
                static_cast<std::uint64_t>(projectedWeights.size()) *
                    sizeof(float),
                0u);
            (void)Graphics::SubmitBufferUpload(
                *Device,
                resources.Displacements,
                displacements.data(),
                static_cast<std::uint64_t>(displacements.size()) *
                    sizeof(float),
                0u);
            (void)Graphics::SubmitBufferUpload(
                *Device,
                resources.Diagnostics,
                &diagnostics,
                sizeof(diagnostics),
                0u);
            (void)Graphics::SubmitBufferUpload(
                *Device,
                resources.State,
                &state,
                sizeof(state),
                0u);
        }

        void RecordProducerCommands(
            RHI::ICommandContext& commandContext)
        {
            if (!EnsurePipelines() ||
                !AllocateResources(
                    *Buffers, Active->Plan, Active->Resources))
            {
                CompleteFallback(
                    "Point-cloud consolidation Vulkan pipelines or bounded resources are unavailable.");
                return;
            }
            UploadInputs();
            const auto uploaded = RHI::MemoryAccess::TransferWrite;
            const auto shaderReadWrite = RHI::MemoryAccess::ShaderRead |
                RHI::MemoryAccess::ShaderWrite;
            for (const RHI::BufferHandle handle : {
                     Active->Resources.State,
                     Active->Resources.SourcePositions,
                     Active->Resources.SourceWeights,
                     Active->Resources.ProjectedB,
                     Active->Resources.ProjectedWeights,
                     Active->Resources.Displacements,
                     Active->Resources.Diagnostics})
            {
                commandContext.BufferBarrier(
                    handle, uploaded, shaderReadWrite);
            }

            if (!RecordGrid(commandContext, true, 0u))
            {
                CompleteFallback(
                    "Point-cloud consolidation Vulkan source-grid scan recording failed.");
                return;
            }
            if (Active->Plan.DensityWeighted)
            {
                Dispatch(
                    commandContext,
                    Pipelines.Density,
                    Push(0u, 0u),
                    Active->Plan.SourceCount);
                commandContext.BufferBarrier(
                    Active->Resources.SourceWeights,
                    RHI::MemoryAccess::ShaderWrite,
                    RHI::MemoryAccess::ShaderRead);
            }

            Dispatch(
                commandContext,
                Pipelines.Initialize,
                Push(0u, 0u),
                Active->Plan.TargetCount);
            commandContext.BufferBarrier(
                Active->Resources.ProjectedA,
                RHI::MemoryAccess::ShaderWrite,
                RHI::MemoryAccess::ShaderRead);
            commandContext.BufferBarrier(
                Active->Resources.Diagnostics,
                RHI::MemoryAccess::ShaderWrite,
                shaderReadWrite);

            for (std::uint32_t iteration = 0u;
                 iteration < Active->Snapshot.Params.MaxIterations;
                 ++iteration)
            {
                if (!RecordGrid(commandContext, false, iteration))
                {
                    CompleteFallback(
                        "Point-cloud consolidation Vulkan projected-grid scan recording failed.");
                    return;
                }
                if (Active->Plan.DensityWeighted)
                {
                    Dispatch(
                        commandContext,
                        Pipelines.Density,
                        Push(iteration, 1u),
                        Active->Plan.TargetCount);
                    commandContext.BufferBarrier(
                        Active->Resources.ProjectedWeights,
                        RHI::MemoryAccess::ShaderWrite,
                        RHI::MemoryAccess::ShaderRead);
                    commandContext.BufferBarrier(
                        Active->Resources.Diagnostics,
                        RHI::MemoryAccess::ShaderWrite,
                        shaderReadWrite);
                }

                Dispatch(
                    commandContext,
                    Pipelines.IterationReset,
                    Push(iteration, 1u),
                    1u);
                commandContext.BufferBarrier(
                    Active->Resources.Diagnostics,
                    RHI::MemoryAccess::ShaderWrite,
                    shaderReadWrite);
                Dispatch(
                    commandContext,
                    Pipelines.Project,
                    Push(iteration, 1u),
                    Active->Plan.TargetCount);
                const RHI::BufferHandle next = (iteration & 1u) == 0u
                    ? Active->Resources.ProjectedB
                    : Active->Resources.ProjectedA;
                commandContext.BufferBarrier(
                    next,
                    RHI::MemoryAccess::ShaderWrite,
                    RHI::MemoryAccess::ShaderRead);
                commandContext.BufferBarrier(
                    Active->Resources.Displacements,
                    RHI::MemoryAccess::ShaderWrite,
                    RHI::MemoryAccess::ShaderRead);
                commandContext.BufferBarrier(
                    Active->Resources.Diagnostics,
                    RHI::MemoryAccess::ShaderWrite,
                    shaderReadWrite);
                Dispatch(
                    commandContext,
                    Pipelines.IterationFinalize,
                    Push(iteration, 1u),
                    1u);
                commandContext.BufferBarrier(
                    Active->Resources.Diagnostics,
                    RHI::MemoryAccess::ShaderWrite,
                    shaderReadWrite);
            }

            Dispatch(
                commandContext,
                Pipelines.FinalReduce,
                Push(
                    Active->Snapshot.Params.MaxIterations - 1u,
                    1u),
                1u);
            commandContext.BufferBarrier(
                Active->Resources.Diagnostics,
                RHI::MemoryAccess::ShaderWrite,
                RHI::MemoryAccess::ShaderRead);
            Active->ProducerCompletedFrame =
                Device->GetGlobalFrameNumber() + 1u;
            Active->ProducerSubmitted = true;
        }

        void RecordReadbackCommands(
            RHI::ICommandContext& commandContext)
        {
            const bool finalIsA =
                (Active->Snapshot.Params.MaxIterations & 1u) == 0u;
            const RHI::BufferHandle finalPositions = finalIsA
                ? Active->Resources.ProjectedA
                : Active->Resources.ProjectedB;
            const char* finalName = finalIsA
                ? "LopGpu.ProjectedA"
                : "LopGpu.ProjectedB";
            const std::uint64_t positionBytes =
                static_cast<std::uint64_t>(Active->Plan.TargetCount) *
                sizeof(glm::vec4);
            Active->Ranges = {
                Graphics::GpuTransferReadbackRangeDesc{
                    .Source = finalPositions,
                    .SourceDesc = StorageBufferDesc(
                        positionBytes, finalName),
                    .SourceRange = RHI::BufferRange{
                        .OffsetBytes = 0u,
                        .SizeBytes = positionBytes,
                    },
                    .SourceAccess = RHI::MemoryAccess::ShaderWrite,
                },
                Graphics::GpuTransferReadbackRangeDesc{
                    .Source = Active->Resources.Diagnostics,
                    .SourceDesc = StorageBufferDesc(
                        sizeof(LopGpuDiagnosticsRecord),
                        "LopGpu.Diagnostics"),
                    .SourceRange = RHI::BufferRange{
                        .OffsetBytes = 0u,
                        .SizeBytes = sizeof(LopGpuDiagnosticsRecord),
                    },
                    .SourceAccess = RHI::MemoryAccess::ShaderWrite,
                },
            };
            Active->Ticket = Transfer.ScheduleReadbackBatch(
                commandContext,
                Graphics::GpuTransferReadbackBatchDesc{
                    .Ranges = Active->Ranges});
            if (!Active->Ticket.IsValid())
            {
                CompleteFallback(
                    "Point-cloud consolidation Vulkan final multi-range readback could not be scheduled.");
                return;
            }
            Active->ReadbackSubmitted = true;
        }

        void CompleteReadback(
            const Graphics::GpuTransferReadbackBatchResult& batch)
        {
            const std::span<const std::byte> positionBytes = batch.Bytes(0u);
            const std::span<const std::byte> diagnosticBytes = batch.Bytes(1u);
            if (positionBytes.size_bytes() !=
                    static_cast<std::size_t>(Active->Plan.TargetCount) *
                        sizeof(glm::vec4) ||
                diagnosticBytes.size_bytes() !=
                    sizeof(LopGpuDiagnosticsRecord))
            {
                CompleteFallback(
                    "Point-cloud consolidation Vulkan result byte sizes do not match the frozen dispatch plan.");
                return;
            }

            LopGpuDiagnosticsRecord diagnostics{};
            std::memcpy(
                &diagnostics,
                diagnosticBytes.data(),
                sizeof(diagnostics));
            if (diagnostics.ErrorCode > static_cast<std::uint32_t>(
                    LopGpuAlgorithmError::NotConverged) ||
                diagnostics.Iterations >
                    Active->Snapshot.Params.MaxIterations ||
                !std::isfinite(diagnostics.AverageDisplacement))
            {
                CompleteFallback(
                    "Point-cloud consolidation Vulkan diagnostics are structurally invalid.");
                return;
            }

            const auto error = static_cast<LopGpuAlgorithmError>(
                diagnostics.ErrorCode);
            const float maximumDisplacement =
                std::bit_cast<float>(diagnostics.MaxDisplacementBits);
            if ((error == LopGpuAlgorithmError::None ||
                 error == LopGpuAlgorithmError::NotConverged) &&
                (diagnostics.Active != 0u ||
                 diagnostics.Iterations == 0u ||
                 diagnostics.SourceGridBuilt != 1u ||
                 diagnostics.ProjectedGridBuilds !=
                     diagnostics.Iterations ||
                 !(diagnostics.AverageDisplacement >= 0.0f) ||
                 !std::isfinite(maximumDisplacement) ||
                 !(maximumDisplacement >= 0.0f)))
            {
                CompleteFallback(
                    "Point-cloud consolidation Vulkan completion diagnostics do not prove a complete source grid and projection sequence.");
                return;
            }
            Consolidation::Result consolidated{};
            consolidated.State = GeometryStatus(error);
            consolidated.Diagnostics.Implementation =
                kLopGpuImplementation;
            consolidated.Diagnostics.Strategy = Consolidation::Kind(
                Active->Snapshot.Params.Method);
            consolidated.Diagnostics.InputPointCount =
                Active->Plan.SourceCount;
            consolidated.Diagnostics.OutputPointCount =
                Active->Plan.TargetCount;
            consolidated.Diagnostics.Iterations = diagnostics.Iterations;
            consolidated.Diagnostics.Converged =
                diagnostics.Converged != 0u;
            consolidated.Diagnostics.UsedDensityWeighting =
                Active->Plan.DensityWeighted;
            consolidated.Diagnostics.AttractionContributionCount =
                diagnostics.AttractionContributionCount;
            consolidated.Diagnostics.RepulsionContributionCount =
                diagnostics.RepulsionContributionCount;
            consolidated.Diagnostics.DensityContributionCount =
                diagnostics.DensityContributionCount;
            consolidated.Diagnostics.EmptyNeighborhoodCount =
                diagnostics.EmptyNeighborhoodCount;
            consolidated.Diagnostics.AverageDisplacement =
                diagnostics.AverageDisplacement;
            consolidated.Diagnostics.MaxDisplacement =
                maximumDisplacement;

            if (error == LopGpuAlgorithmError::None ||
                error == LopGpuAlgorithmError::NotConverged)
            {
                std::vector<glm::vec4> packed(Active->Plan.TargetCount);
                std::memcpy(
                    packed.data(),
                    positionBytes.data(),
                    positionBytes.size_bytes());
                consolidated.Positions.resize(Active->Plan.TargetCount);
                for (std::uint32_t index = 0u;
                     index < Active->Plan.TargetCount;
                     ++index)
                {
                    const glm::vec3 point{packed[index]};
                    if (!std::isfinite(point.x) ||
                        !std::isfinite(point.y) ||
                        !std::isfinite(point.z))
                    {
                        CompleteFallback(
                            "Point-cloud consolidation Vulkan positions contain a non-finite value.");
                        return;
                    }
                    consolidated.Positions[index] = point;
                }
            }

            PointCloudConsolidationSnapshot snapshot =
                std::move(Active->Snapshot);
            const std::string diagnostic = error == LopGpuAlgorithmError::None
                ? std::string{}
                : "Vulkan algorithm completed with geometry status " +
                      std::string{Consolidation::DebugName(
                          consolidated.State)} +
                      ".";
            Completed = PointCloudConsolidationGpuResult{
                .Status =
                    PointCloudConsolidationGpuResultStatus::Completed,
                .Snapshot = std::move(snapshot),
                .Consolidated = std::move(consolidated),
                .Diagnostic = diagnostic,
            };
            Active.reset();
        }

        void CompleteFallback(std::string diagnostic)
        {
            if (!Active.has_value())
                return;
            if (Active->Ticket.IsValid())
                (void)Transfer.CancelReadbackBatch(Active->Ticket);
            Completed = PointCloudConsolidationGpuResult{
                .Status = PointCloudConsolidationGpuResultStatus::
                    FallbackRequired,
                .Snapshot = std::move(Active->Snapshot),
                .Diagnostic = std::move(diagnostic),
            };
            // A recording failure can occur after earlier passes were already
            // appended to the current frame command buffer. Retain their
            // leases until module shutdown/device idle instead of releasing
            // storage that the pending command buffer still references.
            if (!Active->Resources.Leases.empty())
            {
                RetiredResources.push_back(
                    std::move(Active->Resources));
            }
            Active.reset();
        }

        RHI::IDevice* Device{nullptr};
        RHI::BufferManager* Buffers{nullptr};
        Graphics::GpuTransfer Transfer;
        LopGpuPipelineSet Pipelines{};
        std::optional<ActiveOperation> Active{};
        std::optional<PointCloudConsolidationGpuResult> Completed{};
        std::vector<LopGpuResources> RetiredResources{};
    };

    PointCloudConsolidationGpuState::PointCloudConsolidationGpuState(
        RHI::IDevice& device,
        RHI::BufferManager& buffers,
        RHI::ITransferQueue& transferQueue)
        : m_Impl(std::make_unique<Impl>(
              device, buffers, transferQueue))
    {
    }

    PointCloudConsolidationGpuState::~PointCloudConsolidationGpuState() =
        default;

    PointCloudConsolidationGpuSubmission
    PointCloudConsolidationGpuState::Start(
        PointCloudConsolidationSnapshot& snapshot)
    {
        return m_Impl != nullptr
            ? m_Impl->Start(snapshot)
            : PointCloudConsolidationGpuSubmission{
                  .Diagnostic =
                      "Point-cloud consolidation Vulkan state is unavailable.",
              };
    }

    void PointCloudConsolidationGpuState::RecordFrameCommands(
        RHI::ICommandContext& commandContext)
    {
        if (m_Impl != nullptr)
            m_Impl->RecordFrameCommands(commandContext);
    }

    void PointCloudConsolidationGpuState::DrainCompletedTransfers()
    {
        if (m_Impl != nullptr)
            m_Impl->DrainCompletedTransfers();
    }

    std::optional<PointCloudConsolidationGpuResult>
    PointCloudConsolidationGpuState::ConsumeCompleted()
    {
        return m_Impl != nullptr
            ? m_Impl->ConsumeCompleted()
            : std::nullopt;
    }

    bool PointCloudConsolidationGpuState::HasInFlightWork() const noexcept
    {
        return m_Impl != nullptr && m_Impl->HasBusyState();
    }
}
