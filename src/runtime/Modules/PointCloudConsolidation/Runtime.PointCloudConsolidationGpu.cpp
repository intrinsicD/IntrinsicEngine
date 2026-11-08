module;
#include <functional>
#include <chrono>
#include <atomic>
#include <mutex>
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

import Extrinsic.Core.IntegerMath;
import Geometry.Validation;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.StepRuns;
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
#include "Services/Runtime.WorkerGpuPages.hpp"

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
            auto leaseOr = buffers.CreateScratch(desc);
            if (!leaseOr.has_value())
                return false;
            RHI::BufferManager::BufferLease lease =
                std::move(*leaseOr);
            handle = lease.GetHandle();
            leases.push_back(std::move(lease));
            return true;
        }

        // Every buffer is a method scratch lease; releasing `resources` returns them to the cache.
        [[nodiscard]] bool AllocateResources(RHI::BufferManager& buffers,
            const LopGpuPlan& plan,
            LopGpuResources& resources,
            RHI::BufferHandle resident = {})
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
                return CreateBuffer(buffers, StorageBufferDesc(bytes, name), allocated.*role, allocated.Leases);
            };
            const bool created = acquire(&LopGpuResources::State, sizeof(LopGpuStateBufferRecord), "LopGpu.State") &&
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
                    CreateBuffer(buffers,
                        Graphics::BuildParallelPrimitiveScratchBufferDesc(plan.ScanPlan, "LopGpu.ScanScratch"),
                        allocated.ScanScratch,
                        allocated.Leases));
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
        // Publish is a host-only step of the terminal pages: the front's copy rides the first
        // Reduce page; the last Project page of an iteration also records Finalize. A live
        // preview is a copy of its own (WorkerPreview) into a slot the owner offers. WLOP adds row
        // pages of density weights: the sources' after Upload (Init), the projected samples'
        // after every Grid page (part of the iteration).
        enum class LopPhase
        {
            Upload,
            SourceDensity,
            Initialize,
            Grid,
            ProjectedDensity,
            Project,
            Publish,
            Reduce,
            Readback
        };

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
            bool Discarded{}, Ready{}, Accepting{}, TerminalPage{}, PageRecorded{}, PublishPending{};
            // ARCH-023: every run is a step run (as K-Means) whose controls decide each next iteration. It rests
            // (`Resting`: no step or page) at an iteration boundary until it may go on, is stopped or accepted. One
            // logical step is a whole iteration, however many pages it takes. `Partial`: the last iteration left the
            // device active (stopped or accepted from a rest), so the result is not converged. `CommitRequested`:
            // Accept (the run's Commit) prepares and reads positions:
            // in place through the GPU-positions Accept, else a readback published by the module.
            // `BudgetDiagnostic`: the method scratch budget refused the working set (the run fails, no fallback).
            std::string Label{}, BudgetDiagnostic{};
            StepRunsService* Steps{};
            std::uint64_t Run{};
            StepRunDriver Driver{};
            bool Resting{}, CommitRequested{}, Partial{};
            LopGpuPlan Plan{};
            LopGpuResources Resources{};
            // ARCH-023: the iterations run as the step run's worker Step (WorkerStep), each waiting for its own
            // pages; the owner (main thread) keeps live ECS, residency, publication, the terminal pages, Accept and
            // every allocation and release. Init (the first drain, before any segment: Initialized) allocates the
            // working set and uploads its initial values; while a worker segment runs only the worker touches the
            // page planner (Phase, Row, Iteration, SubmittedRows, MaximumCandidates, Partial) and records with the
            // Device, Buffers and Pipelines captured at Init (never this module's state). Iterations mirrors
            // Iteration for observation; an end decided while a segment runs (Ending) applies once it returned.
            std::shared_ptr<WorkerGpuPages> Pages{};
            RHI::IDevice* Device{};
            RHI::BufferManager* Buffers{};
            LopGpuPipelineSet Pipelines{};
            // Resolved at Init (pages make no device call): the state record's address and the grid scans'.
            std::uint64_t StateAddress{};
            std::uint64_t SourceCountsAddress{}, SourceOffsetsAddress{}, ProjectedCountsAddress{},
                ProjectedOffsetsAddress{}, ScanScratchAddress{};
            bool Initialized{}, RecordedFailure{}, PreviewFailed{};
            std::string WorkerDiagnostic{};
            std::atomic<std::uint32_t> Iterations{};
            std::uint32_t FoldedSubmissions{};
            std::uint64_t FoldedReadbackBytes{};
            std::optional<std::pair<PointCloudConsolidationRunStatus, std::string>> Ending{};
            // The live preview's write slot of an in-place run, offered by the owner and borrowed by a preview copy.
            StepPreviewMailbox<Graphics::GpuPropertyView> Mailbox{};
        };

        explicit Impl(RHI::IDevice& device) : Device(&device) {}

        ~Impl()
        {
            *Alive = false;
            // The run's step run ends discarded and drops its callables (they capture this state), also with a page
            // in flight (its recorder no longer records, Alive). A worker step submits nothing more before the
            // pipelines go (its wait gives up).
            if (Active)
            {
                Active->Pages->Seal();
                Active->Driver.Abandon();
            }
            if (Active && Active->PositionRun)
                DiscardEditorGpuPositionRun(Active->Commands, Active->PositionRun, Residency());
            DestroyPipelines();
        }

        [[nodiscard]] bool HasBusyState() const noexcept
        {
            return bool(Active) || Completed.has_value();
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
            // Every run allocates from the context's method scratch cache.
            Buffers = context.SpatialIndices ? context.SpatialIndices->MethodScratch() : nullptr;
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
            if (StepRuns == nullptr)
                return {.Diagnostic = "Point-cloud consolidation step runs are unavailable."};
            const bool lop = snapshot.Request.Config.Strategy == PointCloudConsolidationStrategy::Lop;
            const char* const label = lop ? "LOP" : "WLOP";
            EditorGpuPositionRunHandle run;
            auto commands = BindEditorProcessingCommands(context);
            PointCloudConsolidationResult result = prepared;
            // LOP and isotropic WLOP read the canonical resident positions; an in-place run of the same cardinality
            // publishes through the GPU-positions Accept, any other output through a readback the module publishes.
            auto* residency = context.SpatialIndices ? context.SpatialIndices->PropertyResidency() : nullptr;
            if (!residency || !context.Scene)
                return {.Diagnostic = "Resident consolidation requires an operational property residency."};
            const auto entity = SelectionController::ToEntityHandle(snapshot.Request.StableEntityId);
            const auto before = residency->Stats();
            std::optional<Graphics::GpuPropertyView> input = ResolveGpuPropertyInput(
                *residency, *context.Scene, context.World, entity, snapshot.Request.Properties.InputPositions);
            result.GpuInputUploadBytes = residency->Stats().UploadBytes - before.UploadBytes;
            result.GpuInputCacheHits = residency->Stats().Hits - before.Hits;
            if (!input || input->Layout.Count != plan->SourceCount)
                return {.Refused = true,
                    .Diagnostic =
                        std::string{"Resident "} + label + " input acquisition refused; previous positions retained."};
            if (plan->SourceCount == plan->TargetCount &&
                snapshot.Request.Properties.InputPositions == snapshot.Request.Properties.OutputPositions)
            {
                std::string why;
                run = BeginEditorGpuPositionRun(commands,
                    snapshot.Request.StableEntityId,
                    snapshot.Request.Properties.OutputPositions,
                    *residency,
                    why);
                if (!run)
                    return {.Refused = true, .Diagnostic = std::move(why)};
            }
            Active = std::make_shared<ActiveOperation>();
            Active->Snapshot = std::move(snapshot);
            Active->Context = context;
            Active->Commands = std::move(commands);
            Active->PositionRun = std::move(run);
            Active->Input = std::move(input);
            Active->Result = std::move(result);
            Active->Label = label;
            Active->Plan = std::move(*plan);
            Active->Pages = std::make_shared<WorkerGpuPages>(*Device);
            // Preparation and CPU fallback need these rows only before GPU admission; a new output keeps the
            // snapshot's output state for the module's publication.
            std::vector<glm::vec3>{}.swap(Active->Snapshot.Positions);
            std::vector<glm::vec3>{}.swap(Active->Snapshot.GpuInitialPositions);
            if (Active->PositionRun)
                Active->Back = EditorGpuPositionRunFirstBack(Active->PositionRun);
            auto& r = Active->Result;
            const auto& a = *Active;
            r.Correlation = a.Snapshot.Correlation;
            r.World = a.Snapshot.World;
            r.StableEntityId = a.Snapshot.Request.StableEntityId;
            r.Properties = a.Snapshot.Request.Properties;
            r.Config = a.Snapshot.Request.Config;
            r.RequestedBackend = r.ActualBackend = PointCloudConsolidationBackend::VulkanCompute;
            r.StrategyToken = std::string{StableToken(a.Snapshot.Request.Config.Strategy)};
            r.ImplementationId = std::string(kLopGpuImplementation);
            r.InputPointCount = a.Plan.SourceCount;
            r.OutputPointCount = a.Plan.TargetCount;
            r.ResolvedSupportRadius = a.Snapshot.Params.SupportRadius;
            if (!Active->PositionRun)
                r.BackendDiagnostic =
                    "Live preview shows only an in-place output; this run shows its result at Accept.";
            if (std::string refusal; !StartStepRun(refusal))
            {
                const auto refused = std::move(Active);
                if (refused->PositionRun)
                    DiscardEditorGpuPositionRun(
                        refused->Commands, refused->PositionRun, *refused->Context.SpatialIndices->PropertyResidency());
                return {.Refused = true, .Diagnostic = std::move(refusal)};
            }
            return PointCloudConsolidationGpuSubmission{.Accepted = true};
        }

        // The run's step run: its holder is a job of the run's lane; each step is one whole projection iteration on a
        // worker (the first one also records the Init pages), its pages waited for there; its Commit prepares the
        // terminal positions and accepts them (answered through the driver), its Discard ends the run once no step or
        // page is in flight, its Restart initializes again once nothing is in flight. It starts as its execution
        // options say (running, or ready for a first step); no segment starts before the owner's Init.
        bool StartStepRun(std::string& refusal)
        {
            auto& a = *Active;
            const std::weak_ptr<ActiveOperation> weak = Active;
            const auto& request = a.Snapshot.Request;
            const EditorJobIdentity identity{.EntityId = request.StableEntityId,
                .Scope = ToEditorJobScope(request.Properties.OutputPositions.Domain),
                .OutputSemantic = GeometryPresentationSlotSemantic::Displacement,
                .OutputName = request.Properties.OutputPositions.Name,
                .RequestedDomain = EditorJobDomain::GpuCompute};
            const auto mine = [this, alive = Alive, correlation = a.Snapshot.Correlation]
            {
                return *alive && Active && Active->Snapshot.Correlation == correlation;
            };
            a.Steps = StepRuns;
            a.Run =
                StepRuns->Start({.Label = a.Label,
                                    .Owner = "Runtime.PointCloudConsolidationModule",
                                    .Output = {.StableEntityId = request.StableEntityId,
                                        .Property = request.Properties.OutputPositions},
                                    // ARCH-023: no frame paces the iterations; previews PreviewEvery iterations apart
                                    // when the owner offered a free slot (an in-place run only).
                                    .Execution = request.Config.Execution,
                                    .MetricNames = {"max_displacement", "", "", ""}},
                    {.Step =
                            [op = Active](StepRunMetric& metric)
                        {
                            return WorkerStep(*op, metric);
                        },
                        .Preview =
                            [op = Active](StepPreview& preview)
                        {
                            return WorkerPreview(*op, preview);
                        },
                        .Show =
                            [this, alive = Alive, weak](const StepPreview& preview)
                        {
                            const auto op = weak.lock();
                            return *alive && op && Active == op ? Show(*op, preview) : StepRunShow::Never;
                        },
                        .Commit =
                            [this, mine](std::string& diagnostic)
                        {
                            if (!mine())
                            {
                                diagnostic = "The consolidation run ended.";
                                return StepRunCommit::Refused;
                            }
                            Active->CommitRequested = true; // AdvanceLop prepares the terminal front, then accepts it
                            return StepRunCommit::Pending;
                        },
                        .Discard =
                            [this, mine]
                        {
                            if (!mine())
                                return;
                            Active->Discarded = true; // ends once no page is in flight
                            AdvanceResident();
                        },
                        // run_control restart, called once no page is in flight: the working set stays leased and the
                        // next page initializes it again from the resident input (weights, grid, density, samples), so
                        // iteration 1 follows; the preview shown stays until the new computation replaces it.
                        .Restart =
                            [this, mine](std::string& diagnostic)
                        {
                            if (!mine())
                            {
                                diagnostic = "The consolidation run ended.";
                                return false;
                            }
                            auto& r = *Active;
                            if (r.Accepting || r.CommitRequested || r.Page)
                            {
                                diagnostic = "The consolidation run is being accepted.";
                                return false;
                            }
                            ClearMailbox(r);
                            r.Phase = LopPhase::Upload;
                            r.Row = r.Iteration = r.SubmittedRows = 0u;
                            r.Iterations.store(0u);
                            r.MaximumCandidates = 1u;
                            r.Ready = r.TerminalPage = r.PublishPending = r.Partial = r.Resting = false;
                            r.Result.Iterations = 0u;
                            r.Result.Converged = false;
                            // The working set's initial values again (the next step's Upload page copies the input).
                            if (r.Initialized)
                                UploadInputs(r);
                            r.Pages->ClearAbort();
                            return true;
                        },
                        .Prepared =
                            [weak]
                        {
                            const auto op = weak.lock();
                            return op && op->Initialized;
                        }},
                    EditorStepRunJobs(a.Context.JobCommands, identity, std::make_shared<JobToken>()),
                    refusal);
            if (a.Run == 0u)
                return false;
            a.Driver = StepRuns->Driver(a.Run);
            a.Result.StepRun = a.Run;
            return true;
        }

        // The step run ended discarded (a discard or cancel won over a pending Accept).
        [[nodiscard]] static bool StepRunDiscarded(const ActiveOperation& a)
        {
            if (!a.Steps)
                return false;
            const auto run = a.Steps->Observe(a.Run);
            return run && run->Phase == StepRunPhase::Discarded;
        }

        // Stop, the iteration cap or convergence ended the step run (it is not discarded).
        [[nodiscard]] static bool Ended(const ActiveOperation& a)
        {
            if (!a.Steps)
                return false;
            const auto run = a.Steps->Observe(a.Run);
            return run && run->End != StepRunEnd::None && !IsTerminal(run->Phase);
        }

        // The run's end as its step run sees it: an accepted publication answers the pending Commit; a cancel or a
        // stale source discards it; a failure fails its Commit or its iteration. Ignored by an ended step run.
        static void Tell(ActiveOperation& a, const PointCloudConsolidationRunStatus status, const std::string& message)
        {
            if (!a.Steps)
                return;
            std::string refusal;
            if (status == PointCloudConsolidationRunStatus::Applied)
                a.Driver.CommitFinished(true);
            else if (a.CommitRequested && status != PointCloudConsolidationRunStatus::Cancelled)
                a.Driver.CommitFinished(false, message);
            else if (status == PointCloudConsolidationRunStatus::GeometryProcessingFailed)
                (void)a.Driver.Stepped({}, StepOutcome::Failed);
            else
                (void)a.Steps->Control(a.Run, StepRunAction::Discard, 1u, refusal);
            a.Driver.Recording(false); // every page completed; a terminal step run retires
        }

        Graphics::GpuPropertyResidency& Residency() { return *Active->Context.SpatialIndices->PropertyResidency(); }

        // A worker step of the run may be running (it touches the planner and the working set).
        [[nodiscard]] static bool WorkerBusy(const ActiveOperation& a)
        {
            const auto run = a.Steps ? a.Steps->Observe(a.Run) : std::nullopt;
            return run && run->WorkerInFlight;
        }

        // The worker's page and readback counts, folded into the result (owner).
        static void Fold(ActiveOperation& a)
        {
            const std::uint32_t submissions = a.Pages->Submitted();
            a.Result.GpuSubmissions += submissions - a.FoldedSubmissions;
            a.FoldedSubmissions = submissions;
            const std::uint64_t bytes = a.Pages->ReadbackBytes();
            a.Result.CpuStageReadbackBytes += bytes - a.FoldedReadbackBytes;
            a.FoldedReadbackBytes = bytes;
        }

        // Residency slots worker pages read or wrote, stamped with every compute submission so far before their
        // views (and leases) go.
        void StampAndRelease(ActiveOperation& a, std::optional<Graphics::GpuPropertyView> view = {})
        {
            auto* residency = a.Context.SpatialIndices ? a.Context.SpatialIndices->PropertyResidency() : nullptr;
            if (residency == nullptr)
                return;
            const auto frame = Device->GetGlobalFrameNumber();
            residency->BeginSubmission();
            for (const auto* used : {a.Input ? &*a.Input : nullptr, view ? &*view : nullptr})
                if (used != nullptr)
                    residency->NoteUse(used->Buffer, frame);
            residency->EndSubmission(Device->GetTransferQueue().ComputeFence());
        }

        // Empties the preview mailbox (owner, no worker step running), stamping its slot first.
        void ClearMailbox(ActiveOperation& a)
        {
            if (auto slot = a.Mailbox.Clear())
                StampAndRelease(a, std::move(slot->first));
        }

        void FinishLop(PointCloudConsolidationRunStatus status, std::string message)
        {
            if (!Active) return;
            // A worker step may still record: its pages stop now (its wait gives up), the end applies once the
            // segment returned (AdvanceLop).
            if (WorkerBusy(*Active))
            {
                Active->Pages->Abort();
                if (!Active->Ending)
                    Active->Ending.emplace(status, std::move(message));
                return;
            }
            const auto finishedRun = std::move(Active); // Discard may synchronously deliver the pending Accept sink.
            auto& finished = *finishedRun;
            finished.Pages->Seal();
            Fold(finished);
            ClearMailbox(finished);
            StampAndRelease(finished, finished.Back);
            auto result = finished.Result;
            result.Status = status;
            result.Message = std::move(message);
            result.Error = status == PointCloudConsolidationRunStatus::Applied && !finished.Partial
                ? Core::ErrorCode::Success
                : !finished.BudgetDiagnostic.empty() ? Core::ErrorCode::ResourceBudgetExceeded
                                                     : Core::ErrorCode::InvalidState;
            if (finished.Partial && status == PointCloudConsolidationRunStatus::Applied)
                result.Message = "GPU " + finished.Label + " stopped before convergence; completed positions accepted.";
            Tell(finished, status, result.Message);
            // A failed recorder may already have appended commands. Keep the existing
            // retirement policy: neither pool buffers nor resident leases can recycle
            // before the participant's device-idle shutdown.
            if (((finished.PageRecorded && finished.Page && finished.Page->State == SpatialQueryState::Failed) ||
                    finished.RecordedFailure) &&
                !finished.Resources.Leases.empty())
            {
                RetiredResources.push_back(std::move(finished.Resources));
                if (finished.Input) RetiredViews.push_back(*finished.Input);
                if (finished.Back) RetiredViews.push_back(*finished.Back);
            }
            // Pages are serial and an abandoned worker page was given up: the buffers return to the cache, which
            // reuses them only after the compute fence and the frames in flight retired.
            else
                finished.Resources = {};
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
            if (!Active->PositionRun)
            {
                // A new output: the next page reads the positions back for the module's publication.
                Active->Accepting = true;
                Active->Phase = LopPhase::Readback;
                return;
            }
            if (!EditorGpuPositionRunCurrent(Active->Commands, Active->PositionRun)) return;
            Active->Accepting = true;
            // The completion sink may synchronously destroy Active on rejection.
            const auto commands = Active->Commands;
            const auto run = Active->PositionRun;
            // The step run decides at the real publication boundary (after the readback, before the rows are
            // written), as K-Means' and the shared transactions' do: a discard that won before it publishes nothing.
            const auto accepted = AcceptEditorGpuPositionRun(
                commands,
                run,
                Residency(),
                "Consolidate point set",
                [this, alive = Alive, run](EditorGpuPositionAcceptResult accepted)
                {
                    if (!*alive || !Active || Active->PositionRun != run)
                        return;
                    if (accepted.Status == EditorCommandStatus::Applied || accepted.Status == EditorCommandStatus::NoChange)
                    {
                        FinishLop(
                            PointCloudConsolidationRunStatus::Applied, "GPU " + Active->Label + " positions accepted.");
                    }
                    else if (StepRunDiscarded(*Active))
                        FinishLop(PointCloudConsolidationRunStatus::Cancelled,
                            "GPU " + Active->Label + " discarded; previous positions retained.");
                    else
                        FinishLop(AcceptFailureStatus(accepted.Status), accepted.Message);
                },
                {},
                [driver = Active->Driver]() mutable
                {
                    return driver.Publishing();
                });
            if (Active && Active->PositionRun == run && accepted.Status != EditorCommandStatus::Pending)
                FinishLop(AcceptFailureStatus(accepted.Status), accepted.Message);
        }

        // The panel's buttons are the step run's controls (they change only the step run's state here; a discard
        // ends the run on the next pump). Accept only while it can publish now; a new output's staleness is
        // checked when the module publishes it.
        PointCloudConsolidationGpuObservation GpuRun(CommandCorrelationId correlation, PointCloudConsolidationGpuAction action)
        {
            if (!Active || Active->Snapshot.Correlation != correlation)
                return {};
            auto& a = *Active;
            Fold(a);
            const std::uint32_t iterations = a.Iterations.load(std::memory_order_acquire);
            const bool resting = a.Resting && !a.Ready && iterations > 0u;
            const bool accepting = a.Accepting || a.CommitRequested;
            const bool ready = a.Ready || resting;
            const bool current = !a.PositionRun || EditorGpuPositionRunCurrent(a.Commands, a.PositionRun);
            const bool canAccept = ready && !accepting && current;
            std::string refusal;
            if (action == PointCloudConsolidationGpuAction::Stop)
                (void)a.Steps->Control(a.Run, StepRunAction::Stop, 1u, refusal);
            if (action == PointCloudConsolidationGpuAction::Discard)
                (void)a.Steps->Control(a.Run, StepRunAction::Discard, 1u, refusal);
            if (action == PointCloudConsolidationGpuAction::Accept && canAccept)
                (void)a.Steps->Control(a.Run, StepRunAction::Accept, 1u, refusal);
            return {.Correlation = correlation,
                .Running = !ready && !accepting,
                .ReadyToAccept = ready,
                .Accepting = accepting,
                .CanAccept = canAccept,
                .Message = !current ? "Positions changed or attachment ended; discard this result."
                    : accepting     ? "Reading the GPU positions back."
                    : a.Ready
                    ? (a.Partial ? "GPU " + a.Label + " stopped before convergence; ready to Accept or Discard."
                                 : std::string{"GPU result ready to Accept or Discard."})
                    : resting   ? "Paused at an iteration boundary; Accept prepares its positions."
                    : a.Resting ? "Paused before its first iteration."
                                : "GPU " + a.Label + " pages running.",
                .StepRun = a.Run,
                .Iterations = iterations,
                .Submissions = a.Result.GpuSubmissions,
                .Previews = a.Result.GpuPreviews,
                .InputUploadBytes = a.Result.GpuInputUploadBytes,
                .InputCacheHits = a.Result.GpuInputCacheHits,
                .CpuStageUploadBytes = a.Result.CpuStageUploadBytes,
                .CpuStageReadbackBytes = a.Result.CpuStageReadbackBytes};
        }

        static std::uint32_t LopPageRows(const ActiveOperation& a)
        {
            return std::max(1u, a.Paging.PagePairs / a.MaximumCandidates);
        }

        // The rows the current row phase covers: the sources' density, else every projected sample.
        [[nodiscard]] static std::uint32_t PhaseRows(const ActiveOperation& a) noexcept
        {
            return a.Phase == LopPhase::SourceDensity ? a.Plan.SourceCount : a.Plan.TargetCount;
        }

        [[nodiscard]] static bool RowPhase(const LopPhase phase) noexcept
        {
            return phase == LopPhase::SourceDensity || phase == LopPhase::Initialize ||
                phase == LopPhase::ProjectedDensity || phase == LopPhase::Project;
        }

        // True for the Project page covering the last rows of the current iteration.
        [[nodiscard]] static bool FinalizesIteration(const ActiveOperation& a) noexcept
        {
            return a.Phase == LopPhase::Project && a.Plan.TargetCount - a.Row == a.SubmittedRows;
        }

        // The current positions into `back` (a live preview's slot, or the terminal front). Commands only.
        static void RecordPublishCopy(
            const ActiveOperation& a, RHI::ICommandContext& commands, const Graphics::GpuPropertyView& back)
        {
            const auto rw = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite;
            const auto source = (a.Iteration & 1u) ? a.Resources.ProjectedB : a.Resources.ProjectedA;
            commands.BufferBarrier(source, RHI::MemoryAccess::ShaderWrite, RHI::MemoryAccess::TransferRead);
            commands.CopyBuffer(source, back.Buffer, 0u, 0u, back.Bytes);
            commands.BufferBarrier(source, RHI::MemoryAccess::TransferRead, rw);
            commands.BufferBarrier(back.Buffer, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::ShaderRead);
        }

        // Records the page the planner (Phase, Row, SubmittedRows) names. Commands only, with the device, buffers and
        // pipelines captured at Init, so the worker's iteration pages and the owner's terminal pages record here
        // alike (the owner's recorder notes the residency slots it uses). The working set exists (Init).
        static RHI::BufferHandle RecordLopPage(ActiveOperation& a, RHI::ICommandContext& commands)
        {
            a.PageRecorded = true;
            auto& r = a.Resources;
            const auto rw = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite;
            if (a.Phase != LopPhase::Upload) commands.BufferBarrier(r.Diagnostics, rw | RHI::MemoryAccess::TransferRead, rw);
            if (a.Phase == LopPhase::Upload)
            {
                if (r.SeedRows.IsValid())
                    commands.BufferBarrier(r.SeedRows, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::ShaderRead);
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
                if (!RecordGrid(a, commands, true, 0u))
                    return {};
            }
            else if (a.Phase == LopPhase::SourceDensity || a.Phase == LopPhase::ProjectedDensity)
            {
                // WLOP density weights in row pages: the sources' (Init) and the projected samples' (iteration).
                const bool source = a.Phase == LopPhase::SourceDensity;
                for (std::uint32_t offset = 0u; offset < a.SubmittedRows;)
                {
                    auto push = Push(a, source ? 0u : a.Iteration, source ? 0u : 1u);
                    push.Reserved0 = a.Row + offset;
                    push.PageCount = std::min(LopPageRows(a), a.SubmittedRows - offset);
                    Dispatch(commands, a.Pipelines.Density, push, push.PageCount);
                    offset += push.PageCount;
                }
                for (const auto buffer : {source ? r.SourceWeights : r.ProjectedWeights, r.Diagnostics})
                    commands.BufferBarrier(buffer, RHI::MemoryAccess::ShaderWrite, rw);
            }
            else if (a.Phase == LopPhase::Initialize || a.Phase == LopPhase::Project)
            {
                const auto rows = a.SubmittedRows;
                for (std::uint32_t offset = 0u; offset < rows;)
                {
                    auto push = Push(a, a.Iteration, a.Phase == LopPhase::Initialize ? 0u : 1u);
                    push.Reserved0 = a.Row + offset;
                    push.PageCount = std::min(LopPageRows(a), rows - offset);
                    push.Reserved1 = a.Paging.SubmissionPairs;
                    Dispatch(commands,
                        a.Phase == LopPhase::Initialize ? a.Pipelines.Initialize : a.Pipelines.Project,
                        push,
                        push.PageCount);
                    offset += push.PageCount;
                }
                for (const auto buffer : {r.ProjectedA, r.ProjectedB, r.Displacements, r.Diagnostics})
                    commands.BufferBarrier(buffer, RHI::MemoryAccess::ShaderWrite, rw);
                // Finalize reads only the iteration's diagnostics record (one invocation),
                // so it closes the last projection page instead of costing its own round trip.
                if (FinalizesIteration(a))
                {
                    Dispatch(commands, a.Pipelines.IterationFinalize, Push(a, a.Iteration, 1u), 1u);
                    commands.BufferBarrier(r.Diagnostics, RHI::MemoryAccess::ShaderWrite, rw);
                }
            }
            else if (a.Phase == LopPhase::Grid)
            {
                Dispatch(commands, a.Pipelines.IterationReset, Push(a, a.Iteration, 1u), 1u);
                commands.BufferBarrier(r.Diagnostics, RHI::MemoryAccess::ShaderWrite, rw);
                if (!RecordGrid(a, commands, false, a.Iteration))
                    return {};
            }
            else if (a.Phase == LopPhase::Reduce)
            {
                // The terminal front rides the first Reduce page of an in-place run.
                if (a.PublishPending && a.Back)
                    RecordPublishCopy(a, commands, *a.Back);
                auto push = Push(a, a.Iteration - 1u, 1u);
                push.Reserved0 = a.Row;
                push.PageCount = std::min(a.Paging.ReduceRows, a.Plan.TargetCount - a.Row);
                Dispatch(commands, a.Pipelines.FinalReduce, push, 1u);
                commands.BufferBarrier(r.Diagnostics, RHI::MemoryAccess::ShaderWrite, RHI::MemoryAccess::TransferRead);
            }
            else if (a.Phase == LopPhase::Readback)
            {
                const auto output = (a.Iteration & 1u) ? r.ProjectedB : r.ProjectedA;
                commands.BufferBarrier(output, RHI::MemoryAccess::ShaderWrite, RHI::MemoryAccess::TransferRead);
                return output;
            }
            commands.BufferBarrier(r.Diagnostics, rw, RHI::MemoryAccess::TransferRead);
            return r.Diagnostics;
        }

        // The worker's diagnostics check: an algorithm error other than "not converged" fails the run.
        [[nodiscard]] static bool DiagnosticsFailed(const LopGpuDiagnosticsRecord& diagnostic) noexcept
        {
            return diagnostic.ErrorCode != 0u &&
                diagnostic.ErrorCode != std::uint32_t(LopGpuAlgorithmError::NotConverged);
        }

        // The step run's Step, on a worker: one whole projection iteration (the first one after Init or a restart
        // also records the Init pages: the input upload copy and source grid, WLOP's source density, the samples'
        // initialization), its bounded pages submitted and waited for here in the existing page shape and order.
        // Only the boundary pages' diagnostics come back (row pages request completion only). Owner state is never
        // touched.
        static StepOutcome WorkerStep(ActiveOperation& a, StepRunMetric& metric)
        {
            const auto fail = [&a](std::string why)
            {
                a.WorkerDiagnostic = std::move(why);
                return StepOutcome::Failed;
            };
            for (;;)
            {
                if (a.Pages->Aborted())
                    return StepOutcome::Failed; // discarded, stale or shut down: the owner ends the run
                if (RowPhase(a.Phase))
                    a.SubmittedRows =
                        std::min(PhaseRows(a) - a.Row, std::max(1u, a.Paging.SubmissionPairs / a.MaximumCandidates));
                const bool finalizes = FinalizesIteration(a);
                const bool diagnostics = a.Phase == LopPhase::Upload || a.Phase == LopPhase::Grid || finalizes;
                LopGpuDiagnosticsRecord diagnostic{};
                a.PageRecorded = false;
                switch (a.Pages->Run(
                    [&a](RHI::ICommandContext& commands)
                    {
                        return RecordLopPage(a, commands);
                    },
                    diagnostics ? std::as_writable_bytes(std::span{&diagnostic, 1u}) : std::span<std::byte>{}))
                {
                case WorkerGpuPageStatus::Complete: break;
                case WorkerGpuPageStatus::Aborted:  return StepOutcome::Failed;
                case WorkerGpuPageStatus::Refused:
                    a.RecordedFailure = a.PageRecorded; // a recorder that appended commands keeps the working set
                    return fail("Point-cloud consolidation Vulkan page submission rejected.");
                case WorkerGpuPageStatus::Failed:
                case WorkerGpuPageStatus::TimedOut:
                    return fail("Point-cloud consolidation Vulkan page did not complete: the device was lost.");
                }
                if (diagnostics)
                {
                    if (DiagnosticsFailed(diagnostic))
                        return fail("LOP GPU numerical or neighborhood failure; previous positions retained.");
                    a.MaximumCandidates = std::max(1u, diagnostic.MaximumCandidates);
                }
                if (finalizes)
                {
                    // An iteration completed (its Finalize diagnostics). The device's own end (convergence, or
                    // MaxIterations, which is the step run's cap: Active 0) ends the run; a stop or pause applies
                    // here, at the boundary.
                    a.Row = 0u;
                    ++a.Iteration;
                    a.Iterations.store(a.Iteration, std::memory_order_release);
                    a.Partial = diagnostic.Active != 0u;
                    a.Phase = LopPhase::Grid;
                    metric.Values = {double(std::bit_cast<float>(diagnostic.MaxDisplacementBits)), 0.0, 0.0, 0.0};
                    return diagnostic.Converged != 0u ? StepOutcome::Converged : StepOutcome::Continue;
                }
                if (a.Phase == LopPhase::Upload)
                    a.Phase = a.Plan.DensityWeighted ? LopPhase::SourceDensity : LopPhase::Initialize;
                else if (a.Phase == LopPhase::Grid)
                    a.Phase = a.Plan.DensityWeighted ? LopPhase::ProjectedDensity : LopPhase::Project;
                else if (RowPhase(a.Phase) && (a.Row += a.SubmittedRows) == PhaseRows(a))
                {
                    a.Row = 0u;
                    a.Phase = a.Phase == LopPhase::SourceDensity ? LopPhase::Initialize
                        : a.Phase == LopPhase::ProjectedDensity  ? LopPhase::Project
                                                                 : LopPhase::Grid; // Init done: iteration 1 follows
                }
            }
        }

        // The step run's Preview, after a step on the worker (or at a rest on the owner): the current positions into
        // the slot the owner offered (an in-place run's), as its own submission, not waited for; Show publishes it
        // once the fence completed. No slot offered: no copy (the run goes on).
        static bool WorkerPreview(ActiveOperation& a, StepPreview& preview)
        {
            // The iteration that ends the run (the device's end, or the cap) shows through its terminal front.
            if (!a.Partial || a.Iteration >= a.Snapshot.Request.Config.Execution.IterationCap)
                return false;
            Graphics::GpuPropertyView back{};
            if (!a.Mailbox.Borrow(
                    [&back](const Graphics::GpuPropertyView& slot)
                    {
                        back = slot; // the view alone: the lease stays in the mailbox
                        back.Lease.reset();
                    }))
                return false;
            const auto token = a.Pages->SubmitCopy(
                [&a, &back](RHI::ICommandContext& commands)
                {
                    RecordPublishCopy(a, commands, back);
                    return back.Buffer;
                });
            a.Mailbox.Recorded(token.Value);
            preview.Fence = token.Value;
            return token.IsValid();
        }

        // The step run's Show (owner): a completed copy becomes the front; an incomplete one waits; one no longer in
        // the mailbox (superseded by the terminal front or a restart) is never shown.
        StepRunShow Show(ActiveOperation& a, const StepPreview& preview)
        {
            if (preview.Fence == 0u)
                return StepRunShow::Never;
            if (!Device->GetTransferQueue().IsComplete(RHI::ReadbackToken{preview.Fence}))
                return StepRunShow::Later;
            auto slot = a.Mailbox.Take(preview.Fence);
            if (!slot)
                return StepRunShow::Never;
            slot.reset(); // its lease goes before the publication (the copy completed)
            if (!Residency().Publish(EditorGpuPositionRunKey(a.PositionRun)))
            {
                a.PreviewFailed = true; // the next Advance ends the run (not from inside the run's own pump)
                return StepRunShow::Never;
            }
            ++a.Result.GpuPreviews;
            return StepRunShow::Shown;
        }

        // The Init, on the owner before any segment: pipelines, the working set, the seed rows and the initial values
        // (uploads the first page waits for). False: the run failed (a budget refusal says so).
        bool InitializeLop(ActiveOperation& a)
        {
            auto& r = a.Resources;
            const auto refusals = a.Context.SpatialIndices->Stats().ScratchRefusals;
            if (!EnsurePipelines() || (!r.IsValid() && !AllocateResources(*Buffers, a.Plan, r, a.Input->Buffer)))
            {
                if (a.Context.SpatialIndices->Stats().ScratchRefusals != refusals)
                    a.BudgetDiagnostic = a.Context.SpatialIndices->MethodScratchDiagnostic(
                        refusals, "Point-cloud consolidation Vulkan buffers are unavailable.");
                return false;
            }
            if (a.Plan.TargetCount != a.Plan.SourceCount && !r.SeedRows.IsValid())
            {
                const auto bytes = a.Snapshot.GpuSeedRows.size() * sizeof(std::uint32_t);
                if (!CreateBuffer(*Buffers, StorageBufferDesc(bytes, "LopGpu.SeedRows"), r.SeedRows, r.Leases))
                    return false;
                if (!Graphics::SubmitBufferUpload(*Device, r.SeedRows, a.Snapshot.GpuSeedRows.data(), bytes).Accepted())
                    return false;
                a.Result.CpuStageUploadBytes += bytes;
            }
            std::vector<std::uint32_t>{}.swap(a.Snapshot.GpuSeedRows);
            a.Device = Device;
            a.Buffers = Buffers;
            a.Pipelines = Pipelines;
            const auto address = [this](const RHI::BufferHandle buffer)
            {
                return buffer.IsValid() ? Device->GetBufferDeviceAddress(buffer) : std::uint64_t{};
            };
            a.StateAddress = address(r.State);
            a.SourceCountsAddress = address(r.SourceCounts);
            a.SourceOffsetsAddress = address(r.SourceOffsets);
            a.ProjectedCountsAddress = address(r.ProjectedCounts);
            a.ProjectedOffsetsAddress = address(r.ProjectedOffsets);
            a.ScanScratchAddress = address(r.ScanScratch);
            if (!a.StateAddress || !a.SourceCountsAddress || !a.SourceOffsetsAddress || !a.ProjectedCountsAddress ||
                !a.ProjectedOffsetsAddress || (r.ScanScratch.IsValid() && !a.ScanScratchAddress))
                return false;
            UploadInputs(a);
            a.Initialized = true;
            return true;
        }

        // Frame maintenance: moves the resident run on, then tells its step run whether an owner page is in flight.
        void AdvanceResident()
        {
            AdvanceLop();
            if (Active)
                Active->Driver.Recording(bool(Active->Page));
        }

        // Main thread, frame maintenance (and a discard): the Init, the live preview slot, ends decided while a worker
        // step ran, and once the run ended or is accepted the terminal pages (an in-place run's front, the final
        // reduction, a new output's readback) as owner pages.
        void AdvanceLop()
        {
            if (!Active || !Active->Input) return;
            auto& a = *Active;
            Fold(a);
            if (a.Context.AttachmentActive && !a.Context.AttachmentActive()) a.Discarded = true;
            const auto run = a.Steps->Observe(a.Run);
            const bool busy = run && run->WorkerInFlight;
            if (busy)
            {
                if (a.Discarded || a.Ending)
                    a.Pages->Abort(); // the worker gives up; the end applies once it returned
                else
                    OfferPreview(a, *run);
                a.Resting = false;
                return;
            }
            if (a.Ending)
            {
                auto ending = std::move(*a.Ending);
                a.Ending.reset();
                FinishLop(ending.first, std::move(ending.second));
                return;
            }
            if (a.Page && a.Page->State != SpatialQueryState::Ready && a.Page->State != SpatialQueryState::Failed) return;
            // The run ended on its own side: a failed step (its reason is the worker's), a discard or cancel (also of
            // a pending Commit, whose run has no pump left to call Discard).
            if (run && run->Phase == StepRunPhase::Failed && !a.WorkerDiagnostic.empty() && !a.Discarded)
            {
                FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed, a.WorkerDiagnostic);
                return;
            }
            if (run && IsTerminal(run->Phase) && run->Phase != StepRunPhase::Committed && !a.CommitRequested)
                a.Discarded = true;
            if (a.Discarded)
            {
                FinishLop(PointCloudConsolidationRunStatus::Cancelled,
                    "GPU " + a.Label + " discarded; previous positions retained.");
                return;
            }
            if (a.PreviewFailed)
            {
                FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed,
                    "GPU " + a.Label + " preview publication refused.");
                return;
            }
            if (!a.Initialized)
            {
                if (!InitializeLop(a))
                    FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed,
                        a.BudgetDiagnostic.empty() ? "Point-cloud consolidation Vulkan buffers are unavailable."
                                                   : a.BudgetDiagnostic);
                return;
            }
            if ((a.Snapshot.Request.Config.Execution.AutoAccept != StepRunAutoAccept::Never || a.CommitRequested ||
                    a.Accepting) &&
                a.PositionRun && !EditorGpuPositionRunCurrent(a.Commands, a.PositionRun))
            {
                FinishLop(PointCloudConsolidationRunStatus::StaleSource,
                    "GPU " + a.Label + " inputs changed before Accept; previous positions retained.");
                return;
            }
            if (a.Accepting && a.PositionRun)
                return;
            if (a.Ready && !a.Accepting)
            {
                if (a.CommitRequested)
                    AcceptLop();
                // A manual stale result stays inspectable with Accept disabled; a new output's Accept reads back.
                if (!Active || !Active->Accepting || Active->PositionRun)
                    return;
            }
            if (a.Page)
            {
                if (a.Page->State == SpatialQueryState::Failed)
                {
                    FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed,
                        a.BudgetDiagnostic.empty() ? a.Page->Diagnostic : a.BudgetDiagnostic);
                    return;
                }
                a.Result.CpuStageReadbackBytes += a.Page->Data.size();
                if (a.PublishPending)
                {
                    // The page carrying the terminal front completed, so the back is filled.
                    a.PublishPending = false;
                    if (!Residency().Publish(EditorGpuPositionRunKey(a.PositionRun)))
                    {
                        FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed,
                            "GPU " + a.Label + " preview publication refused.");
                        return;
                    }
                    ++a.Result.GpuPreviews;
                    a.Back.reset();
                }
                if (a.Phase == LopPhase::Reduce)
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
                        if (DiagnosticsFailed(diagnostic))
                        {
                            FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed, "LOP GPU numerical or neighborhood failure; previous positions retained.");
                            return;
                        }
                        const float maximum = std::bit_cast<float>(diagnostic.MaxDisplacementBits);
                        if (diagnostic.Iterations == 0u || diagnostic.Iterations > a.Iteration ||
                            diagnostic.SourceGridBuilt != 1u ||
                            diagnostic.ProjectedGridBuilds != diagnostic.Iterations ||
                            (!a.Partial && diagnostic.Active != 0u) || !std::isfinite(diagnostic.AverageDisplacement) ||
                            diagnostic.AverageDisplacement < 0.0f || !std::isfinite(maximum) || maximum < 0.0f)
                        {
                            FinishLop(PointCloudConsolidationRunStatus::GeometryProcessingFailed,
                                "LOP diagnostics do not describe a complete finite iteration; previous positions retained.");
                            return;
                        }
                        a.Result.Iterations = diagnostic.Iterations;
                        a.Result.Converged = !a.Partial && diagnostic.Converged != 0u;
                        a.Result.AverageDisplacement = diagnostic.AverageDisplacement;
                        a.Result.MaxDisplacement = std::bit_cast<float>(diagnostic.MaxDisplacementBits);
                        a.Result.GeometryStatus = a.Partial
                            ? Consolidation::Status::NotConverged
                            : GeometryStatus(static_cast<LopGpuAlgorithmError>(diagnostic.ErrorCode));
                        a.Ready = true;
                        a.Page.reset();
                        if (a.CommitRequested)
                            AcceptLop(); // Accept (a user's or the batch's) asked for these
                        if (!Active || !Active->Accepting || Active->PositionRun)
                            return;
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
                    result.Diagnostics.UsedDensityWeighting = a.Plan.DensityWeighted;
                    // The module publishes it (one undoable entry) and answers the pending Commit through Driver.
                    // Every worker page completed; the residency slots it read are stamped before the views go.
                    a.Pages->Seal();
                    ClearMailbox(a);
                    StampAndRelease(a, a.Back);
                    a.Driver.Recording(false);
                    Completed = PointCloudConsolidationGpuResult{.Snapshot = std::move(a.Snapshot),
                        .Consolidated = std::move(result),
                        .Metrics = a.Result,
                        .Driver = a.Driver};
                    Active.reset();
                    return;
                }
                a.Page.reset();
            }
            if (!a.TerminalPage)
            {
                // Accept (from a rest), or the run ended (convergence, the cap, Stop): the terminal front of the current
                // positions. Otherwise the run rests or waits for its next segment, and live preview may take a slot.
                if (!a.CommitRequested && !Ended(a))
                {
                    a.Resting = run && run->Phase != StepRunPhase::Running;
                    if (run)
                        OfferPreview(a, *run);
                    return;
                }
                if (a.Iteration == 0u)
                {
                    FinishLop(PointCloudConsolidationRunStatus::Cancelled,
                        "GPU " + a.Label + " stopped before its first iteration; previous positions retained.");
                    return;
                }
                ClearMailbox(a); // a recorded, unpublished live preview is superseded by the terminal front
                a.Resting = false;
                a.TerminalPage = true;
                a.Phase = a.PositionRun ? LopPhase::Publish : LopPhase::Reduce;
            }
            if (a.Phase == LopPhase::Publish)
            {
                if (!a.Back)
                    a.Back = Residency().AcquireBack(EditorGpuPositionRunKey(a.PositionRun), a.Input->Layout, 2u);
                // A terminal front must be retained: a busy ring defers it.
                if (!a.Back)
                    return;
                a.PublishPending = true;
                a.Phase = LopPhase::Reduce;
            }
            const bool diagnostics = a.Phase == LopPhase::Reduce && a.Plan.TargetCount - a.Row <= a.Paging.ReduceRows;
            const std::size_t bytes = diagnostics ? sizeof(LopGpuDiagnosticsRecord) :
                a.Phase == LopPhase::Readback ? std::size_t(a.Plan.TargetCount) * sizeof(glm::vec3) : 0u;
            a.PageRecorded = false;
            a.Page = a.Context.SpatialIndices->QueueGpuCompute(bytes,
                [this, alive = Alive, op = Active](RHI::ICommandContext& commands, const SpatialGpuIndexView&)
                {
                    if (!*alive || op->Discarded)
                        return RHI::BufferHandle{};
                    // The owner's recorder, inside the cache's submission scope: the slots it uses are noted.
                    const auto frame = Device->GetGlobalFrameNumber();
                    Residency().NoteUse(op->Input->Buffer, frame);
                    if (op->PublishPending && op->Back)
                        Residency().NoteUse(op->Back->Buffer, frame);
                    return RecordLopPage(*op, commands);
                });
            ++a.Result.GpuSubmissions;
        }

        // Live preview of an in-place run: a free slot in the mailbox for the worker's next copy (or a rest's), never
        // waited for; a busy ring offers none and that preview is dropped. Preview off acquires nothing.
        void OfferPreview(ActiveOperation& a, const StepRunObservation& run)
        {
            if (!a.PositionRun || !a.Initialized || a.TerminalPage || a.CommitRequested ||
                a.Snapshot.Request.Config.Execution.PreviewEvery == 0u || IsTerminal(run.Phase) ||
                run.Phase == StepRunPhase::Accepting || !a.Mailbox.Empty())
                return;
            auto back = std::exchange(a.Back, std::nullopt); // the position run's first back serves first
            if (!back)
                back = Residency().AcquireBack(EditorGpuPositionRunKey(a.PositionRun), a.Input->Layout, 2u);
            if (back)
                (void)a.Mailbox.Offer(std::move(*back));
        }

        // Both frame callbacks move the resident run on: a completed page is consumed and the next one queued.
        void RecordFrameCommands(RHI::ICommandContext&)
        {
            if (Active && !Completed && Device != nullptr && Buffers != nullptr)
                AdvanceResident();
        }

        void DrainCompletedTransfers()
        {
            if (Active)
                AdvanceResident();
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

        [[nodiscard]] static LopGpuPushConstants Push(
            const ActiveOperation& active, const std::uint32_t iteration, const std::uint32_t passMode)
        {
            const PointCloudConsolidationConfig& config =
                active.Snapshot.Request.Config;
            return LopGpuPushConstants{
                .StateBDA = active.StateAddress,
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
                .RepulsionWeight = static_cast<float>(active.Snapshot.Params.RepulsionWeight),
                .ConvergenceTolerance = static_cast<float>(active.Snapshot.Params.ConvergenceTolerance),
            };
        }

        static void Dispatch(RHI::ICommandContext& commandContext,
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
                std::max(Core::CeilDiv(elementCount, kLopGpuGroupSize), 1u),
                1u,
                1u);
        }

        // Commands only, with the device, buffers and pipelines captured at Init (a worker step records here).
        [[nodiscard]] static bool RecordGrid(ActiveOperation& active,
            RHI::ICommandContext& commandContext,
            const bool source,
            const std::uint32_t iteration)
        {
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

            const LopGpuPushConstants push = Push(active, iteration, source ? 0u : 1u);
            Dispatch(commandContext,
                active.Pipelines.GridCount,
                push,
                source ? active.Plan.SourceCount : active.Plan.TargetCount);
            commandContext.BufferBarrier(
                counts,
                RHI::MemoryAccess::ShaderWrite,
                RHI::MemoryAccess::ShaderRead);

            Graphics::GpuParallelPrimitiveRecordResult scan =
                Graphics::RecordGpuPrefixScan(Graphics::GpuPrefixScanRecordDesc{
                    .Device = active.Device,
                    .CommandContext = &commandContext,
                    .Buffers = active.Buffers,
                    .Pipelines = active.Pipelines.Parallel,
                    .Input = counts,
                    .Output = offsets,
                    .Scratch = resources.ScanScratch,
                    .ElementCount = active.Plan.CellCount,
                    .Mode = Graphics::PrefixScanMode::Exclusive,
                    .InputAddress = source ? active.SourceCountsAddress : active.ProjectedCountsAddress,
                    .OutputAddress = source ? active.SourceOffsetsAddress : active.ProjectedOffsetsAddress,
                    .ScratchAddress = active.ScanScratchAddress,
                });
            if (!scan.Succeeded() || !scan.Recorded)
                return false;

            Dispatch(commandContext,
                active.Pipelines.GridScatter,
                push,
                source ? active.Plan.SourceCount : active.Plan.TargetCount);
            commandContext.BufferBarrier(
                indices,
                RHI::MemoryAccess::ShaderWrite,
                RHI::MemoryAccess::ShaderRead);
            return true;
        }

        // The working set's initial values (the positions are the resident input, copied by the Upload page): owner
        // uploads the next compute page waits for.
        void UploadInputs(ActiveOperation& active)
        {
            LopGpuResources& resources = active.Resources;
            const std::vector<float> sourceWeights(active.Plan.SourceCount, 1.0f);
            const std::vector<float> projectedWeights(active.Plan.TargetCount, 1.0f);
            const std::vector<float> displacements(active.Plan.TargetCount, 0.0f);
            const LopGpuDiagnosticsRecord diagnostics{
                .Active = 1u,
            };
            active.Result.CpuStageUploadBytes += sizeof(LopGpuStateBufferRecord) + sizeof(diagnostics) +
                (sourceWeights.size() + projectedWeights.size() + displacements.size()) * sizeof(float);
            const auto address = [this](const RHI::BufferHandle buffer)
            {
                return buffer.IsValid() ? Device->GetBufferDeviceAddress(buffer) : 0u;
            };
            const LopGpuStateBufferRecord state{
                .SourcePositionsBDA = address(resources.SourcePositions),
                .SourceWeightsBDA = address(resources.SourceWeights),
                .ProjectedABDA = address(resources.ProjectedA),
                .ProjectedBBDA = address(resources.ProjectedB),
                .ProjectedWeightsBDA = address(resources.ProjectedWeights),
                .DisplacementsBDA = address(resources.Displacements),
                .SourceCountsBDA = address(resources.SourceCounts),
                .SourceOffsetsBDA = address(resources.SourceOffsets),
                .SourceCursorsBDA = address(resources.SourceCursors),
                .SourceIndicesBDA = address(resources.SourceIndices),
                .ProjectedCountsBDA = address(resources.ProjectedCounts),
                .ProjectedOffsetsBDA = address(resources.ProjectedOffsets),
                .ProjectedCursorsBDA = address(resources.ProjectedCursors),
                .ProjectedIndicesBDA = address(resources.ProjectedIndices),
                .DiagnosticsBDA = address(resources.Diagnostics),
                .SeedRowsBDA = address(resources.SeedRows),
            };
            const auto upload = [this](const RHI::BufferHandle buffer, const void* data, const std::uint64_t bytes)
            {
                (void)Graphics::SubmitBufferUpload(*Device, buffer, data, bytes, 0u);
            };
            upload(resources.SourceWeights, sourceWeights.data(), sourceWeights.size() * sizeof(float));
            upload(resources.ProjectedWeights, projectedWeights.data(), projectedWeights.size() * sizeof(float));
            upload(resources.Displacements, displacements.data(), displacements.size() * sizeof(float));
            upload(resources.Diagnostics, &diagnostics, sizeof(diagnostics));
            upload(resources.State, &state, sizeof(state));
        }

        std::shared_ptr<bool> Alive{std::make_shared<bool>(true)};
        StepRunsService* StepRuns{};
        RHI::IDevice* Device{nullptr};
        RHI::BufferManager* Buffers{nullptr}; // the SpatialIndexCache method scratch the last Start found
        LopGpuPipelineSet Pipelines{};
        std::shared_ptr<ActiveOperation> Active{};
        std::optional<PointCloudConsolidationGpuResult> Completed{};
        std::vector<LopGpuResources> RetiredResources{};
        std::vector<Graphics::GpuPropertyView> RetiredViews{};
    };

    PointCloudConsolidationGpuState::PointCloudConsolidationGpuState(RHI::IDevice& device)
        : m_Impl(std::make_unique<Impl>(device))
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
        // A run waiting for its user (ready, or resting without a page) has nothing in flight.
        if (m_Impl == nullptr)
            return false;
        const auto& a = m_Impl->Active;
        return !m_Impl->RetiredResources.empty() || m_Impl->Completed.has_value() ||
            (a && (a->Accepting || a->Page || (!a->Ready && !a->Resting)));
    }

    void PointCloudConsolidationGpuState::UseStepRuns(StepRunsService* steps) noexcept
    {
        if (m_Impl)
            m_Impl->StepRuns = steps;
    }
    bool PointCloudConsolidationGpuState::HasPendingRun() const noexcept
    {
        return m_Impl && m_Impl->HasBusyState();
    }
}
