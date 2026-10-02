module;
#include <functional>
#include <chrono>
#include "Modules/PointCloudConsolidation/Runtime.LopPaging.TestSupport.hpp"

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
#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.PointCloudConsolidationModule;

import Geometry.Validation;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.ECS.Scene.Registry;
import Geometry.Properties;
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
import Geometry.PointLBVH;

#include "Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationGpu.Internal.hpp"

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
            std::uint64_t SeedRowsBDA{};
        };
        static_assert(sizeof(LopGpuStateBufferRecord) == 128u);

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
            std::uint32_t PageCount{}, Reserved1{};
        };
        static_assert(sizeof(LopGpuPushConstants) == 88u);

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
            std::uint32_t MaximumCandidates{0u};
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
                    Geometry::Validation::IsFinite(Origin) &&
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
            RHI::BufferHandle SeedRows{};
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

        using LopBufferRole = RHI::BufferHandle LopGpuResources::*;
        constexpr LopBufferRole kLopBufferRoles[] = {
            &LopGpuResources::State, &LopGpuResources::SourcePositions,
            &LopGpuResources::SourceWeights, &LopGpuResources::ProjectedA,
            &LopGpuResources::ProjectedB, &LopGpuResources::ProjectedWeights,
            &LopGpuResources::Displacements, &LopGpuResources::SourceCounts,
            &LopGpuResources::SourceOffsets, &LopGpuResources::SourceCursors,
            &LopGpuResources::SourceIndices, &LopGpuResources::ProjectedCounts,
            &LopGpuResources::ProjectedOffsets, &LopGpuResources::ProjectedCursors,
            &LopGpuResources::ProjectedIndices, &LopGpuResources::Diagnostics,
            &LopGpuResources::ScanScratch, &LopGpuResources::SeedRows,
        };

        [[nodiscard]] std::vector<RHI::BufferManager::BufferLease>::iterator FindLease(
            LopGpuResources& resources, const RHI::BufferHandle handle)
        {
            return std::ranges::find_if(resources.Leases,
                [handle](const auto& lease) { return lease.GetHandle() == handle; });
        }

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

        // Moves the spare buffer of `role` into `resources` when its capacity
        // still covers `desc`, otherwise releases it and creates a new one. Spare
        // buffers come only from completed runs, so no GPU work references them.
        [[nodiscard]] bool AcquireBuffer(
            RHI::BufferManager& buffers,
            LopGpuResources& spare,
            const LopBufferRole role,
            const RHI::BufferDesc& desc,
            LopGpuResources& resources)
        {
            const RHI::BufferHandle retained = std::exchange(spare.*role, RHI::BufferHandle{});
            if (const auto lease = FindLease(spare, retained); lease != spare.Leases.end())
            {
                const RHI::BufferDesc* current = buffers.GetDesc(retained);
                const bool fits = current != nullptr && desc.SizeBytes > 0u &&
                    current->SizeBytes >= desc.SizeBytes;
                if (fits)
                {
                    resources.*role = retained;
                    resources.Leases.push_back(std::move(*lease));
                }
                spare.Leases.erase(lease);
                if (fits)
                    return true;
            }
            return CreateBuffer(buffers, desc, resources.*role, resources.Leases);
        }

        // Returns a completed run's owned buffers to the spare set. A resident
        // input view has no lease here, so its address is never kept as scratch.
        void RecycleResources(LopGpuResources&& done, LopGpuResources& spare)
        {
            for (const LopBufferRole role : kLopBufferRoles)
            {
                const auto lease = FindLease(done, done.*role);
                if (!(done.*role).IsValid() || lease == done.Leases.end() || (spare.*role).IsValid())
                    continue;
                spare.*role = done.*role;
                spare.Leases.push_back(std::move(*lease));
                done.Leases.erase(lease);
            }
        }

        [[nodiscard]] bool AllocateResources(
            RHI::BufferManager& buffers,
            const LopGpuPlan& plan,
            LopGpuResources& spare,
            LopGpuResources& resources, RHI::BufferHandle resident = {})
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
            allocated.SourcePositions = resident;
            const auto acquire = [&](const LopBufferRole role, const std::uint64_t bytes, const char* name)
            {
                return AcquireBuffer(buffers, spare, role, StorageBufferDesc(bytes, name), allocated);
            };
            const bool created =
                acquire(&LopGpuResources::State, sizeof(LopGpuStateBufferRecord), "LopGpu.State") &&
                (resident.IsValid() ||
                 acquire(&LopGpuResources::SourcePositions, sourceVec4Bytes, "LopGpu.SourcePositions")) &&
                acquire(&LopGpuResources::SourceWeights, sourceFloatBytes, "LopGpu.SourceWeights") &&
                acquire(&LopGpuResources::ProjectedA, targetVec4Bytes, "LopGpu.ProjectedA") &&
                acquire(&LopGpuResources::ProjectedB, targetVec4Bytes, "LopGpu.ProjectedB") &&
                acquire(&LopGpuResources::ProjectedWeights, targetFloatBytes, "LopGpu.ProjectedWeights") &&
                acquire(&LopGpuResources::Displacements, targetFloatBytes, "LopGpu.Displacements") &&
                acquire(&LopGpuResources::SourceCounts, cellBytes, "LopGpu.SourceCounts") &&
                acquire(&LopGpuResources::SourceOffsets, cellBytes, "LopGpu.SourceOffsets") &&
                acquire(&LopGpuResources::SourceCursors, cellBytes, "LopGpu.SourceCursors") &&
                acquire(&LopGpuResources::SourceIndices, sourceIndexBytes, "LopGpu.SourceIndices") &&
                acquire(&LopGpuResources::ProjectedCounts, cellBytes, "LopGpu.ProjectedCounts") &&
                acquire(&LopGpuResources::ProjectedOffsets, cellBytes, "LopGpu.ProjectedOffsets") &&
                acquire(&LopGpuResources::ProjectedCursors, cellBytes, "LopGpu.ProjectedCursors") &&
                acquire(&LopGpuResources::ProjectedIndices, targetIndexBytes, "LopGpu.ProjectedIndices") &&
                acquire(&LopGpuResources::Diagnostics, sizeof(LopGpuDiagnosticsRecord), "LopGpu.Diagnostics") &&
                (plan.ScanPlan.ScratchBytes == 0u ||
                 AcquireBuffer(buffers, spare, &LopGpuResources::ScanScratch,
                     Graphics::BuildParallelPrimitiveScratchBufferDesc(plan.ScanPlan, "LopGpu.ScanScratch"),
                     allocated));
            if (!created || !allocated.IsValid())
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
        // Publish is a host-only step: the preview copy rides the next Grid or Reduce
        // page, and the last Project page of an iteration also records Finalize.
        enum class LopPhase { Upload, Initialize, Grid, Project, Publish, Reduce, Readback };

        struct ActiveOperation
        {
            PointCloudConsolidationSnapshot Snapshot{};
            EditorProcessingContext Context{};
            EditorProcessingCommands Commands{};
            EditorGpuPositionRunHandle PositionRun{};
            std::optional<Graphics::GpuPropertyView> Input{}, Back{};
            std::shared_ptr<SpatialGpuResult> Page{};
            PointCloudConsolidationResult Result{};
            LopPhase Phase{LopPhase::Upload};
            std::uint32_t Row{}, Iteration{}, MaximumCandidates{1u}, SubmittedRows{};
            LopPagingLimits Paging{LopPagingForTesting};
            bool Stop{}, Discarded{}, Ready{}, Accepting{}, TerminalPage{}, PageRecorded{}, PublishPending{};
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
            *Alive = false;
            if (Active.has_value() && Active->Ticket.IsValid())
                (void)Transfer.CancelReadbackBatch(Active->Ticket);
            if (Active && Active->PositionRun)
                DiscardEditorGpuPositionRun(Active->Commands, Active->PositionRun, Residency());
            DestroyPipelines();
        }

        [[nodiscard]] bool HasBusyState() const noexcept
        {
            return Active.has_value() || Completed.has_value();
        }

        [[nodiscard]] PointCloudConsolidationGpuSubmission Start(
            PointCloudConsolidationSnapshot& snapshot, const EditorProcessingContext& context,
            const PointCloudConsolidationResult& prepared)
        {
            if (HasBusyState())
            {
                return PointCloudConsolidationGpuSubmission{
                    .Refused = snapshot.Request.Config.Strategy == PointCloudConsolidationStrategy::Lop,
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
            const bool lop = snapshot.Request.Config.Strategy == PointCloudConsolidationStrategy::Lop;
            std::optional<Graphics::GpuPropertyView> input;
            EditorGpuPositionRunHandle run;
            auto commands = BindEditorProcessingCommands(context);
            PointCloudConsolidationResult result = prepared;
            if (lop)
            {
                auto* residency = context.SpatialIndices ? context.SpatialIndices->PropertyResidency() : nullptr;
                if (!residency || !context.Scene)
                    return {.Diagnostic = "Resident LOP requires an operational property residency."};
                const auto entity = SelectionController::ToEntityHandle(snapshot.Request.StableEntityId);
                const auto before = residency->Stats();
                input = ResolveGpuPropertyInput(*residency, *context.Scene, context.World, entity,
                                                snapshot.Request.Properties.InputPositions);
                result.GpuInputUploadBytes = residency->Stats().UploadBytes - before.UploadBytes;
                result.GpuInputCacheHits = residency->Stats().Hits - before.Hits;
                if (!input || input->Layout.Count != plan->SourceCount)
                    return {.Refused = true, .Diagnostic = "Resident LOP input acquisition refused; previous positions retained."};
                std::string why;
                if (plan->SourceCount == plan->TargetCount &&
                    snapshot.Request.Properties.InputPositions == snapshot.Request.Properties.OutputPositions)
                {
                    run = BeginEditorGpuPositionRun(commands, snapshot.Request.StableEntityId,
                        snapshot.Request.Properties.OutputPositions, *residency, why);
                    if (!run) return {.Refused = true, .Diagnostic = std::move(why)};
                }
            }
            Active = ActiveOperation{.Snapshot = std::move(snapshot), .Context = context,
                .Commands = std::move(commands), .PositionRun = std::move(run), .Input = std::move(input),
                .Result = std::move(result), .Plan = std::move(*plan)};
            if (lop)
            {
                // Preparation and CPU fallback need these rows only before GPU admission.
                std::vector<glm::vec3>{}.swap(Active->Snapshot.Positions);
                std::vector<glm::vec3>{}.swap(Active->Snapshot.GpuInitialPositions);
                if (Active->PositionRun) Active->Back = EditorGpuPositionRunFirstBack(Active->PositionRun);
                auto& r = Active->Result;
                const auto& a = *Active;
                r.Correlation = a.Snapshot.Correlation;
                r.World = a.Snapshot.World;
                r.StableEntityId = a.Snapshot.Request.StableEntityId;
                r.Properties = a.Snapshot.Request.Properties;
                r.Config = a.Snapshot.Request.Config;
                r.RequestedBackend = r.ActualBackend = PointCloudConsolidationBackend::VulkanCompute;
                r.StrategyToken = "lop";
                r.ImplementationId = std::string(kLopGpuImplementation);
                r.InputPointCount = a.Plan.SourceCount;
                r.OutputPointCount = a.Plan.TargetCount;
                r.ResolvedSupportRadius = a.Snapshot.Params.SupportRadius;
            }
            return PointCloudConsolidationGpuSubmission{.Accepted = true};
        }

        Graphics::GpuPropertyResidency& Residency() { return *Active->Context.SpatialIndices->PropertyResidency(); }

        void FinishLop(PointCloudConsolidationRunStatus status, std::string message)
        {
            if (!Active) return;
            auto finished = std::move(*Active);
            Active.reset(); // Discard may synchronously deliver the pending Accept sink.
            auto result = finished.Result;
            result.Status = status;
            result.Message = std::move(message);
            result.Error = status == PointCloudConsolidationRunStatus::Applied && !finished.Stop
                ? Core::ErrorCode::Success : Core::ErrorCode::InvalidState;
            if (finished.Stop && status == PointCloudConsolidationRunStatus::Applied)
                result.Message = "GPU LOP stopped before convergence; completed positions accepted.";
            // A failed recorder may already have appended commands. Keep the existing
            // retirement policy: neither pool buffers nor resident leases can recycle
            // before the participant's device-idle shutdown.
            if (finished.PageRecorded && finished.Page && finished.Page->State == SpatialQueryState::Failed &&
                !finished.Resources.Leases.empty())
            {
                RetiredResources.push_back(std::move(finished.Resources));
                if (finished.Input) RetiredViews.push_back(*finished.Input);
                if (finished.Back) RetiredViews.push_back(*finished.Back);
            }
            // Pages are serial, so every recorded page has completed here.
            else RecycleResources(std::move(finished.Resources), Spare);
            // Discard is idempotent after Accept bound the front to the CPU revision.
            if (finished.PositionRun) DiscardEditorGpuPositionRun(finished.Commands, finished.PositionRun, *finished.Context.SpatialIndices->PropertyResidency());
            Completed = PointCloudConsolidationGpuResult{.Published = std::move(result)};
        }

        // A stale Accept is StaleSource whichever path observes it first: the accept job's
        // unpublished finalizer (completion drain) or AdvanceLop's own current check.
        [[nodiscard]] static PointCloudConsolidationRunStatus AcceptFailureStatus(const EditorCommandStatus status) noexcept
        {
            return status == EditorCommandStatus::StaleEntity ? PointCloudConsolidationRunStatus::StaleSource
                                                              : PointCloudConsolidationRunStatus::GeometryProcessingFailed;
        }

        void AcceptLop()
        {
            if (!Active || !Active->Ready || Active->Accepting) return;
            if (!EditorGpuPositionRunCurrent(Active->Commands, Active->PositionRun)) return;
            Active->Accepting = true;
            // The completion sink may synchronously destroy Active on rejection.
            const auto commands = Active->Commands;
            const auto run = Active->PositionRun;
            const auto accepted = AcceptEditorGpuPositionRun(commands, run,
                Residency(), "Consolidate point set", [this, alive = Alive, run](EditorGpuPositionAcceptResult accepted) {
                    if (!*alive || !Active || Active->PositionRun != run) return;
                    if (accepted.Status == EditorCommandStatus::Applied || accepted.Status == EditorCommandStatus::NoChange)
                    {
                        FinishLop(PointCloudConsolidationRunStatus::Applied, "GPU LOP positions accepted.");
                    }
                    else FinishLop(AcceptFailureStatus(accepted.Status), accepted.Message);
                });
            if (Active && Active->PositionRun == run && accepted.Status != EditorCommandStatus::Pending)
                FinishLop(AcceptFailureStatus(accepted.Status), accepted.Message);
        }

        PointCloudConsolidationGpuObservation GpuRun(CommandCorrelationId correlation, PointCloudConsolidationGpuAction action)
        {
            if (!Active || !Active->PositionRun || Active->Snapshot.Correlation != correlation) return {};
            if (action == PointCloudConsolidationGpuAction::Stop && !Active->Ready) Active->Stop = true;
            if (action == PointCloudConsolidationGpuAction::Discard)
            {
                Active->Discarded = true;
                AdvanceLop();
            }
            if (action == PointCloudConsolidationGpuAction::Accept) AcceptLop();
            if (!Active) return {};
            const auto& a = *Active;
            const bool current = EditorGpuPositionRunCurrent(a.Commands, a.PositionRun);
            return {.Correlation = correlation, .Running = !a.Ready && !a.Discarded,
                .ReadyToAccept = a.Ready && !a.Discarded, .Accepting = a.Accepting,
                .CanAccept = a.Ready && !a.Accepting && current && !a.Discarded,
                .Message = !current ? "Positions changed or attachment ended; discard this result." :
                    a.Accepting ? "Reading the GPU positions back." :
                    a.Ready ? (a.Stop ? "GPU LOP stopped before convergence; preview ready to Accept or Discard."
                                      : "GPU preview ready to Accept or Discard.") : "GPU LOP pages running.",
                .Iterations = a.Iteration, .Submissions = a.Result.GpuSubmissions, .Previews = a.Result.GpuPreviews,
                .InputUploadBytes = a.Result.GpuInputUploadBytes, .InputCacheHits = a.Result.GpuInputCacheHits,
                .CpuStageUploadBytes = a.Result.CpuStageUploadBytes, .CpuStageReadbackBytes = a.Result.CpuStageReadbackBytes};
        }

        std::uint32_t LopPageRows() const
        {
            return std::max(1u, Active->Paging.PagePairs / Active->MaximumCandidates);
        }

        // True for the Project page covering the last rows of the current iteration.
        [[nodiscard]] bool FinalizesIteration() const noexcept
        {
            const auto& a = *Active;
            return a.Phase == LopPhase::Project && a.Plan.TargetCount - a.Row == a.SubmittedRows;
        }

        void RecordPublishCopy(RHI::ICommandContext& commands)
        {
            auto& a = *Active;
            if (!a.PublishPending || !a.Back) return;
            const auto rw = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite;
            const auto source = (a.Iteration & 1u) ? a.Resources.ProjectedB : a.Resources.ProjectedA;
            commands.BufferBarrier(source, RHI::MemoryAccess::ShaderWrite, RHI::MemoryAccess::TransferRead);
            commands.CopyBuffer(source, a.Back->Buffer, 0u, 0u, a.Back->Bytes);
            commands.BufferBarrier(source, RHI::MemoryAccess::TransferRead, rw);
            commands.BufferBarrier(a.Back->Buffer, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::ShaderRead);
            Residency().NoteUse(a.Back->Buffer, Device->GetGlobalFrameNumber());
        }

        RHI::BufferHandle RecordLopPage(RHI::ICommandContext& commands)
        {
            if (!Active || Active->Discarded) return {};
            auto& a = *Active;
            a.PageRecorded = true;
            auto& r = a.Resources;
            const auto rw = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite;
            if (a.Phase != LopPhase::Upload) commands.BufferBarrier(r.Diagnostics, rw | RHI::MemoryAccess::TransferRead, rw);
            if (a.Phase == LopPhase::Upload)
            {
                if (!EnsurePipelines() || !AllocateResources(*Buffers, a.Plan, Spare, r, a.Input->Buffer)) return {};
                if (a.Plan.TargetCount != a.Plan.SourceCount)
                {
                    const auto bytes = a.Snapshot.GpuSeedRows.size() * sizeof(std::uint32_t);
                    if (!AcquireBuffer(*Buffers, Spare, &LopGpuResources::SeedRows,
                            StorageBufferDesc(bytes, "LopGpu.SeedRows"), r)) return {};
                    if (!Graphics::SubmitBufferUpload(*Device, r.SeedRows, a.Snapshot.GpuSeedRows.data(), bytes).Accepted()) return {};
                    a.Result.CpuStageUploadBytes += bytes;
                    commands.BufferBarrier(r.SeedRows, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::ShaderRead);
                }
                std::vector<std::uint32_t>{}.swap(a.Snapshot.GpuSeedRows);
                UploadInputs();
                for (const auto buffer : {r.State, r.SourcePositions, r.SourceWeights, r.ProjectedWeights,
                                         r.Displacements, r.Diagnostics})
                    commands.BufferBarrier(buffer, RHI::MemoryAccess::TransferWrite, rw);
                commands.BufferBarrier(r.SourcePositions, rw, RHI::MemoryAccess::TransferRead);
                commands.CopyBuffer(r.SourcePositions, r.ProjectedB, 0u, 0u, std::uint64_t(a.Plan.TargetCount) * 12u);
                commands.BufferBarrier(r.ProjectedB, RHI::MemoryAccess::TransferWrite, rw);
                // Defined rows even when a numerical error terminates an initialization page.
                commands.CopyBuffer(r.SourcePositions, r.ProjectedA, 0u, 0u, std::uint64_t(a.Plan.TargetCount) * 12u);
                commands.BufferBarrier(r.SourcePositions, RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::ShaderRead);
                commands.BufferBarrier(r.ProjectedA, RHI::MemoryAccess::TransferWrite, rw);
                if (!RecordGrid(commands, true, 0u)) return {};
            }
            else if (a.Phase == LopPhase::Initialize || a.Phase == LopPhase::Project)
            {
                const auto rows = a.SubmittedRows;
                for (std::uint32_t offset = 0u; offset < rows;)
                {
                    auto push = Push(a.Iteration, a.Phase == LopPhase::Initialize ? 0u : 1u);
                    push.Reserved0 = a.Row + offset;
                    push.PageCount = std::min(LopPageRows(), rows - offset);
                    push.Reserved1 = a.Paging.SubmissionPairs;
                    Dispatch(commands, a.Phase == LopPhase::Initialize ? Pipelines.Initialize : Pipelines.Project, push, push.PageCount);
                    offset += push.PageCount;
                }
                for (const auto buffer : {r.ProjectedA, r.ProjectedB, r.Displacements, r.Diagnostics})
                    commands.BufferBarrier(buffer, RHI::MemoryAccess::ShaderWrite, rw);
                // Finalize reads only the iteration's diagnostics record (one invocation),
                // so it closes the last projection page instead of costing its own round trip.
                if (FinalizesIteration())
                {
                    Dispatch(commands, Pipelines.IterationFinalize, Push(a.Iteration, 1u), 1u);
                    commands.BufferBarrier(r.Diagnostics, RHI::MemoryAccess::ShaderWrite, rw);
                }
            }
            else if (a.Phase == LopPhase::Grid)
            {
                RecordPublishCopy(commands);
                Dispatch(commands, Pipelines.IterationReset, Push(a.Iteration, 1u), 1u);
                commands.BufferBarrier(r.Diagnostics, RHI::MemoryAccess::ShaderWrite, rw);
                if (!RecordGrid(commands, false, a.Iteration)) return {};
            }
            else if (a.Phase == LopPhase::Reduce)
            {
                RecordPublishCopy(commands);
                auto push = Push(a.Iteration - 1u, 1u);
                push.Reserved0 = a.Row;
                push.PageCount = std::min(a.Paging.ReduceRows, a.Plan.TargetCount - a.Row);
                Dispatch(commands, Pipelines.FinalReduce, push, 1u);
                commands.BufferBarrier(r.Diagnostics, RHI::MemoryAccess::ShaderWrite, RHI::MemoryAccess::TransferRead);
            }
            else if (a.Phase == LopPhase::Readback)
            {
                const auto output = (a.Iteration & 1u) ? r.ProjectedB : r.ProjectedA;
                commands.BufferBarrier(output, RHI::MemoryAccess::ShaderWrite, RHI::MemoryAccess::TransferRead);
                return output;
            }
            commands.BufferBarrier(r.Diagnostics, rw, RHI::MemoryAccess::TransferRead);
            Residency().NoteUse(a.Input->Buffer, Device->GetGlobalFrameNumber());
            return r.Diagnostics;
        }

        void AdvanceLop()
        {
            if (!Active || !Active->Input) return;
            auto& a = *Active;
            if (a.Context.AttachmentActive && !a.Context.AttachmentActive()) a.Discarded = true;
            if (a.Page && a.Page->State != SpatialQueryState::Ready && a.Page->State != SpatialQueryState::Failed) return;
            if (a.Discarded)
            {
                FinishLop(PointCloudConsolidationRunStatus::Cancelled, "GPU LOP discarded; previous positions retained.");
                return;
            }
            if ((a.Snapshot.Request.AutoAccept || a.Accepting) && a.PositionRun &&
                !EditorGpuPositionRunCurrent(a.Commands, a.PositionRun))
            {
                FinishLop(PointCloudConsolidationRunStatus::StaleSource,
                    "GPU LOP inputs changed before automatic Accept; previous positions retained.");
                return;
            }
            if (a.Accepting || a.Ready) return;
            if (a.Page)
            {
                if (a.Page->State == SpatialQueryState::Failed)
                {
                    FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed, a.Page->Diagnostic);
                    return;
                }
                a.Result.CpuStageReadbackBytes += a.Page->Data.size();
                if (a.PublishPending)
                {
                    // The page carrying the preview copy completed, so the back is filled.
                    a.PublishPending = false;
                    if (!Residency().Publish(EditorGpuPositionRunKey(a.PositionRun)))
                    {
                        FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed, "LOP preview publication refused.");
                        return;
                    }
                    ++a.Result.GpuPreviews;
                    a.Back.reset();
                }
                const bool finalized = FinalizesIteration();
                if (a.Phase == LopPhase::Upload || a.Phase == LopPhase::Grid || finalized)
                {
                    if (a.Page->Data.size() != sizeof(LopGpuDiagnosticsRecord))
                    {
                        FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed, "LOP iteration diagnostics readback size mismatch.");
                        return;
                    }
                    LopGpuDiagnosticsRecord diagnostic{};
                    std::memcpy(&diagnostic, a.Page->Data.data(), sizeof(diagnostic));
                    if (diagnostic.ErrorCode != 0u && diagnostic.ErrorCode != std::uint32_t(LopGpuAlgorithmError::NotConverged))
                    {
                        FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed, "LOP GPU numerical or neighborhood failure; previous positions retained.");
                        return;
                    }
                    a.MaximumCandidates = std::max(1u, diagnostic.MaximumCandidates);
                    if (finalized)
                    {
                        a.Row = 0u;
                        ++a.Iteration;
                        a.TerminalPage = a.Stop || diagnostic.Active == 0u || a.Iteration == a.Snapshot.Params.MaxIterations;
                        const bool publish = a.PositionRun && (a.TerminalPage ||
                            a.Iteration % a.Snapshot.Request.Config.GpuPreviewInterval == 0u);
                        a.Phase = publish ? LopPhase::Publish : a.TerminalPage ? LopPhase::Reduce : LopPhase::Grid;
                    }
                    else a.Phase = a.Phase == LopPhase::Upload ? LopPhase::Initialize : LopPhase::Project;
                }
                else if (a.Phase == LopPhase::Initialize || a.Phase == LopPhase::Project)
                {
                    a.Row += a.SubmittedRows;
                    if (a.Row == a.Plan.TargetCount)
                    {
                        a.Row = 0u;
                        a.Phase = LopPhase::Grid;
                    }
                }
                else if (a.Phase == LopPhase::Reduce)
                {
                    a.Row += std::min(a.Paging.ReduceRows, a.Plan.TargetCount - a.Row);
                    if (a.Row == a.Plan.TargetCount)
                    {
                        if (a.Page->Data.size() != sizeof(LopGpuDiagnosticsRecord))
                        {
                            FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed, "LOP diagnostics readback size mismatch.");
                            return;
                        }
                        LopGpuDiagnosticsRecord diagnostic{};
                        std::memcpy(&diagnostic, a.Page->Data.data(), sizeof(diagnostic));
                        if (diagnostic.ErrorCode != 0u && diagnostic.ErrorCode != std::uint32_t(LopGpuAlgorithmError::NotConverged))
                        {
                            FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed, "LOP GPU numerical or neighborhood failure; previous positions retained.");
                            return;
                        }
                        const float maximum = std::bit_cast<float>(diagnostic.MaxDisplacementBits);
                        if (diagnostic.Iterations == 0u || diagnostic.Iterations > a.Iteration ||
                            diagnostic.SourceGridBuilt != 1u || diagnostic.ProjectedGridBuilds != diagnostic.Iterations ||
                            (!a.Stop && diagnostic.Active != 0u) || !std::isfinite(diagnostic.AverageDisplacement) ||
                            diagnostic.AverageDisplacement < 0.0f || !std::isfinite(maximum) || maximum < 0.0f)
                        {
                            FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed,
                                "LOP diagnostics do not describe a complete finite iteration; previous positions retained.");
                            return;
                        }
                        a.Result.Iterations = diagnostic.Iterations;
                        a.Result.Converged = !a.Stop && diagnostic.Converged != 0u;
                        a.Result.AverageDisplacement = diagnostic.AverageDisplacement;
                        a.Result.MaxDisplacement = std::bit_cast<float>(diagnostic.MaxDisplacementBits);
                        a.Result.GeometryStatus = a.Stop ? Consolidation::Status::NotConverged
                            : GeometryStatus(static_cast<LopGpuAlgorithmError>(diagnostic.ErrorCode));
                        if (a.PositionRun)
                        {
                            a.Ready = true;
                            a.Page.reset();
                            if (a.Snapshot.Request.AutoAccept) AcceptLop();
                            return;
                        }
                        a.Phase = LopPhase::Readback;
                    }
                }
                else if (a.Phase == LopPhase::Readback)
                {
                    Consolidation::Result result{};
                    result.State = a.Result.GeometryStatus;
                    result.Positions.resize(a.Plan.TargetCount);
                    if (a.Page->Data.size() != result.Positions.size() * sizeof(glm::vec3))
                    {
                        FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed, "LOP position readback size mismatch.");
                        return;
                    }
                    std::memcpy(result.Positions.data(), a.Page->Data.data(), a.Page->Data.size());
                    if (!std::all_of(result.Positions.begin(), result.Positions.end(), [](glm::vec3 p) {
                        return Geometry::Validation::IsFinite(p);
                    }))
                    {
                        FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed,
                            "LOP readback contains non-finite positions; previous positions retained.");
                        return;
                    }
                    result.Diagnostics.Implementation = kLopGpuImplementation;
                    result.Diagnostics.Strategy = Consolidation::Kind(a.Snapshot.Params.Method);
                    result.Diagnostics.InputPointCount = a.Plan.SourceCount;
                    result.Diagnostics.OutputPointCount = a.Plan.TargetCount;
                    result.Diagnostics.Iterations = a.Result.Iterations;
                    result.Diagnostics.Converged = a.Result.Converged;
                    result.Diagnostics.AverageDisplacement = a.Result.AverageDisplacement;
                    result.Diagnostics.MaxDisplacement = a.Result.MaxDisplacement;
                    Completed = PointCloudConsolidationGpuResult{.Status = PointCloudConsolidationGpuResultStatus::Completed,
                        .Snapshot = std::move(a.Snapshot), .Consolidated = std::move(result), .Metrics = a.Result};
                    RecycleResources(std::move(a.Resources), Spare);
                    Active.reset();
                    return;
                }
                a.Page.reset();
            }
            if (a.Phase == LopPhase::Publish)
            {
                if (!a.Back)
                    a.Back = Residency().AcquireBack(EditorGpuPositionRunKey(a.PositionRun), a.Input->Layout, 2u);
                // A terminal front must be retained; preview ring pressure merely drops a preview.
                if (a.TerminalPage && !a.Back) return;
                a.PublishPending = a.Back.has_value();
                a.Phase = a.TerminalPage ? LopPhase::Reduce : LopPhase::Grid;
            }
            if (a.Phase == LopPhase::Initialize || a.Phase == LopPhase::Project)
                a.SubmittedRows = std::min(a.Plan.TargetCount - a.Row,
                    std::max(1u, a.Paging.SubmissionPairs / a.MaximumCandidates));
            const bool diagnostics = a.Phase == LopPhase::Upload || a.Phase == LopPhase::Grid ||
                FinalizesIteration() ||
                (a.Phase == LopPhase::Reduce && a.Plan.TargetCount - a.Row <= a.Paging.ReduceRows);
            const std::size_t bytes = diagnostics ? sizeof(LopGpuDiagnosticsRecord) :
                a.Phase == LopPhase::Readback ? std::size_t(a.Plan.TargetCount) * sizeof(glm::vec3) : 0u;
            a.PageRecorded = false;
            a.Page = a.Context.SpatialIndices->QueueGpuCompute(bytes,
                [this, alive = Alive](RHI::ICommandContext& commands, const SpatialGpuIndexView&) { return *alive ? RecordLopPage(commands) : RHI::BufferHandle{}; }, SpatialGpuLatency::Immediate);
            ++a.Result.GpuSubmissions;

        }

        void RecordFrameCommands(RHI::ICommandContext& commandContext)
        {
            if (!Active.has_value() || Completed.has_value() ||
                Device == nullptr || Buffers == nullptr)
            {
                return;
            }
            if (Active->Input) { AdvanceLop(); return; }
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
            if (Active && Active->Input) { AdvanceLop(); return; }
            if (!Active.has_value() || !Active->ReadbackSubmitted)
                return;
            RHI::NullCommandContext noop;
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
            constexpr Graphics::ParallelPrimitiveKind scan[] = {
                Graphics::ParallelPrimitiveKind::PrefixScan};
            if (!Graphics::CreateParallelPrimitivePipelines(
                    *Device, Pipelines.Parallel, scan) ||
                !Pipelines.IsValid())
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
            Graphics::DestroyParallelPrimitivePipelines(
                *Device, Pipelines.Parallel);
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
            std::vector<glm::vec4> source(active.Input ? 0u : active.Plan.SourceCount);
            for (std::uint32_t index = 0u;
                 index < source.size();
                 ++index)
            {
                source[index] = glm::vec4{
                    active.Snapshot.Positions[index], 1.0f};
            }
            std::vector<glm::vec4> projected(active.Input ? 0u : active.Plan.TargetCount);
            for (std::uint32_t index = 0u;
                 index < projected.size();
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

            if (active.Input)
                active.Result.CpuStageUploadBytes += sizeof(LopGpuStateBufferRecord) + sizeof(diagnostics) +
                    (sourceWeights.size() + projectedWeights.size() + displacements.size()) * sizeof(float);
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
                .SeedRowsBDA = resources.SeedRows.IsValid() ? Device->GetBufferDeviceAddress(resources.SeedRows) : 0u,
            };

            if (!active.Input)
            {
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
            }
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
                    *Buffers, Active->Plan, Spare, Active->Resources))
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
            // Retained buffers may exceed this run's plan; describe their real shape.
            const auto actualDesc = [this](const RHI::BufferHandle handle, const std::uint64_t bytes, const char* name)
            {
                const RHI::BufferDesc* desc = Buffers->GetDesc(handle);
                return desc != nullptr ? *desc : StorageBufferDesc(bytes, name);
            };
            Active->Ranges = {
                Graphics::GpuTransferReadbackRangeDesc{
                    .Source = finalPositions,
                    .SourceDesc = actualDesc(
                        finalPositions, positionBytes, finalName),
                    .SourceRange = RHI::BufferRange{
                        .OffsetBytes = 0u,
                        .SizeBytes = positionBytes,
                    },
                    .SourceAccess = RHI::MemoryAccess::ShaderWrite,
                },
                Graphics::GpuTransferReadbackRangeDesc{
                    .Source = Active->Resources.Diagnostics,
                    .SourceDesc = actualDesc(
                        Active->Resources.Diagnostics,
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
                    if (!Geometry::Validation::IsFinite(point))
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
            // The readback completed after every producer pass.
            RecycleResources(std::move(Active->Resources), Spare);
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

        std::shared_ptr<bool> Alive{std::make_shared<bool>(true)};
        RHI::IDevice* Device{nullptr};
        RHI::BufferManager* Buffers{nullptr};
        Graphics::GpuTransfer Transfer;
        LopGpuPipelineSet Pipelines{};
        // Buffers of completed runs, kept at their capacity for the next run.
        LopGpuResources Spare{};
        std::optional<ActiveOperation> Active{};
        std::optional<PointCloudConsolidationGpuResult> Completed{};
        std::vector<LopGpuResources> RetiredResources{};
        std::vector<Graphics::GpuPropertyView> RetiredViews{};
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
        PointCloudConsolidationSnapshot& snapshot, const EditorProcessingContext& context,
            const PointCloudConsolidationResult& prepared)
    {
        return m_Impl != nullptr
            ? m_Impl->Start(snapshot, context, prepared)
            : PointCloudConsolidationGpuSubmission{
                  .Diagnostic =
                      "Point-cloud consolidation Vulkan state is unavailable.",
              };
    }

    PointCloudConsolidationGpuObservation PointCloudConsolidationGpuState::GpuRun(
        CommandCorrelationId correlation, PointCloudConsolidationGpuAction action)
    {
        return m_Impl ? m_Impl->GpuRun(correlation, action) : PointCloudConsolidationGpuObservation{};
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
        return m_Impl != nullptr && (!m_Impl->RetiredResources.empty() || m_Impl->Completed.has_value() ||
            (m_Impl->Active && (!m_Impl->Active->Ready || m_Impl->Active->Accepting)));
    }
}
