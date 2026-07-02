module;

#include <cstdint>
#include <string>
#include <vector>

export module Extrinsic.Runtime.KMeansGpuBackend;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;

// ============================================================
// KMeansGpuBackend (GEOM-056 Slice A — planning core)
// ============================================================
// The runtime-owned Vulkan-compute backend seam for Geometry.KMeans. This slice
// ships the pure, CPU-testable planning core only: buffer-role/layout
// computation, the BDA state record + push-constant layouts the future
// kmeans_*.comp shaders consume, per-iteration dispatch-plan computation,
// buffer/pipeline descriptor builders, and a device-aware resolve that reports
// GPU availability and recommends CPU fallback. No RHI recording happens yet;
// `Extrinsic.Runtime.KMeansBackend` routes its Backend::GPU request through the
// resolve seam and still falls back to the geometry CPU reference with honest
// telemetry. Later GEOM-056 slices add the shaders, recorded Lloyd loop,
// persistent buffer reuse, async readback drain (RUNTIME-137), and parity.
//
// Design: docs/migration/kmeans-gpu-vulkan-compute-proposal.md
// ============================================================

export namespace Extrinsic::Runtime
{
    inline constexpr std::uint32_t kKMeansGpuGroupSize = 256u;

    enum class KMeansGpuStatus : std::uint8_t
    {
        Success,
        InvalidInput,
        MissingDevice,
        DeviceUnavailable,
        PlanningOnly,
        SizeOverflow,
        InvalidGpuResource,
    };

    enum class KMeansGpuPassKind : std::uint8_t
    {
        Reset,   // zero per-cluster sums/counts + reduction scratch
        Assign,  // one thread per point: nearest centroid + accumulate
        Update,  // one thread per cluster: mean or empty-cluster reseed
    };

    enum class KMeansGpuBufferRole : std::uint8_t
    {
        None,
        State,
        PositionX,
        PositionY,
        PositionZ,
        Centroids,       // packed vec3, current
        NextCentroids,   // packed vec3, ping-pong target
        SumX,
        SumY,
        SumZ,
        Counts,
        Labels,
        SquaredDistances,
        Reduction,       // inertia bits, max-distance bits, argmax index, changed count
        LabelsReadback,
        SquaredDistancesReadback,
        CentroidsReadback,
    };

    struct KMeansGpuPlanDesc
    {
        std::uint32_t PointCount{0u};
        std::uint32_t ClusterCount{0u};
        std::uint32_t MaxIterations{0u};
        std::uint32_t GroupSize{kKMeansGpuGroupSize};
        float ConvergenceTolerance{1.0e-4f};
    };

    struct KMeansGpuBufferSpan
    {
        KMeansGpuBufferRole Role{KMeansGpuBufferRole::None};
        std::uint64_t OffsetBytes{0u};
        std::uint64_t SizeBytes{0u};
    };

    struct KMeansGpuBufferLayout
    {
        KMeansGpuStatus Status{KMeansGpuStatus::Success};
        std::uint32_t PointCount{0u};
        std::uint32_t ClusterCount{0u}; // effective k = min(requested, PointCount)
        std::uint64_t StateBytes{0u};
        std::uint64_t WorkBufferBytes{0u};
        std::uint64_t LabelsReadbackBytes{0u};
        std::uint64_t SquaredDistancesReadbackBytes{0u};
        std::uint64_t CentroidsReadbackBytes{0u};
        std::vector<KMeansGpuBufferSpan> Spans{}; // sub-spans within the work buffer

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Status == KMeansGpuStatus::Success;
        }
    };

    // Matches the BDA table the planned kmeans_*.comp shaders read via a scalar
    // push constant. All pointers are Buffer Device Addresses.
    struct KMeansGpuStateBufferRecord
    {
        std::uint64_t PositionXBDA{0u};
        std::uint64_t PositionYBDA{0u};
        std::uint64_t PositionZBDA{0u};
        std::uint64_t CentroidsBDA{0u};
        std::uint64_t NextCentroidsBDA{0u};
        std::uint64_t SumXBDA{0u};
        std::uint64_t SumYBDA{0u};
        std::uint64_t SumZBDA{0u};
        std::uint64_t CountsBDA{0u};
        std::uint64_t LabelsBDA{0u};
        std::uint64_t SquaredDistancesBDA{0u};
        std::uint64_t ReductionBDA{0u};
    };
    static_assert(sizeof(KMeansGpuStateBufferRecord) == 96u);

    // Matches the scalar push-constant block of the planned kmeans_*.comp shaders.
    struct KMeansGpuPassPushConstants
    {
        std::uint64_t StateBDA{0u};
        std::uint32_t PointCount{0u};
        std::uint32_t ClusterCount{0u};
        std::uint32_t GroupSize{kKMeansGpuGroupSize};
        std::uint32_t Iteration{0u};
        float ConvergenceTolSquared{0.0f};
        float Reserved0{0.0f};
    };
    static_assert(sizeof(KMeansGpuPassPushConstants) == 32u);

    struct KMeansGpuDispatchDesc
    {
        KMeansGpuPassKind Kind{KMeansGpuPassKind::Reset};
        std::uint32_t Iteration{0u};
        std::uint32_t ElementCount{0u};
        std::uint32_t GroupSize{kKMeansGpuGroupSize};
        std::uint32_t GroupCountX{0u};
        std::uint32_t GroupCountY{1u};
        std::uint32_t GroupCountZ{1u};
    };

    struct KMeansGpuDispatchPlan
    {
        KMeansGpuStatus Status{KMeansGpuStatus::Success};
        std::uint32_t PointCount{0u};
        std::uint32_t ClusterCount{0u};
        std::uint32_t MaxIterations{0u};
        std::uint32_t GroupSize{kKMeansGpuGroupSize};
        KMeansGpuBufferLayout Layout{};
        std::vector<KMeansGpuDispatchDesc> Dispatches{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Status == KMeansGpuStatus::Success;
        }
    };

    struct KMeansGpuResolveDesc
    {
        RHI::IDevice* Device{nullptr};
        KMeansGpuPlanDesc Plan{};
    };

    struct KMeansGpuResolveResult
    {
        KMeansGpuStatus Status{KMeansGpuStatus::Success};
        bool GpuExecutionAvailable{false};
        bool CpuFallbackRecommended{true};
        std::string Diagnostic{};
        KMeansGpuDispatchPlan Plan{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == KMeansGpuStatus::Success;
        }
    };

    [[nodiscard]] const char* DebugNameForKMeansGpuStatus(KMeansGpuStatus status) noexcept;
    [[nodiscard]] const char* DebugNameForKMeansGpuPassKind(KMeansGpuPassKind kind) noexcept;
    [[nodiscard]] const char* DebugNameForKMeansGpuBufferRole(KMeansGpuBufferRole role) noexcept;

    [[nodiscard]] KMeansGpuBufferLayout ComputeKMeansGpuBufferLayout(
        const KMeansGpuPlanDesc& desc) noexcept;

    [[nodiscard]] KMeansGpuDispatchPlan ComputeKMeansGpuDispatchPlan(
        const KMeansGpuPlanDesc& desc);

    [[nodiscard]] RHI::BufferDesc BuildKMeansGpuStateBufferDesc(
        const char* debugName = "KMeansGpu.State") noexcept;

    [[nodiscard]] RHI::BufferDesc BuildKMeansGpuWorkBufferDesc(
        const KMeansGpuBufferLayout& layout,
        const char* debugName = "KMeansGpu.Work") noexcept;

    [[nodiscard]] RHI::BufferDesc BuildKMeansGpuReadbackBufferDesc(
        std::uint64_t sizeBytes,
        const char* debugName = "KMeansGpu.Readback") noexcept;

    [[nodiscard]] RHI::PipelineDesc BuildKMeansResetPipelineDesc(
        const char* shaderPath = "shaders/kmeans_reset.comp.spv");

    [[nodiscard]] RHI::PipelineDesc BuildKMeansAssignPipelineDesc(
        const char* shaderPath = "shaders/kmeans_assign.comp.spv");

    [[nodiscard]] RHI::PipelineDesc BuildKMeansUpdatePipelineDesc(
        const char* shaderPath = "shaders/kmeans_update.comp.spv");

    // Device-aware resolve. Computes the dispatch plan and reports whether an
    // operational GPU execution path is available. In this slice the recorded
    // path does not exist yet, so an operational device resolves to
    // `PlanningOnly` with `CpuFallbackRecommended == true`; later slices flip
    // this to `Success` / `GpuExecutionAvailable == true`.
    [[nodiscard]] KMeansGpuResolveResult ResolveKMeansGpuRequest(
        const KMeansGpuResolveDesc& desc);

    // -------------------------------------------------------------------------
    // Recording (GEOM-056 Slice B) — bind pipelines and record the barrier-
    // chained reset/assign/update Lloyd loop into a command context. Buffers are
    // pre-created and their inputs (positions, seed centroids) pre-uploaded by
    // the caller / execution slice; this seam only records the State BDA table
    // upload and the compute dispatches. No readback here.
    // -------------------------------------------------------------------------

    struct KMeansGpuPipelineSet
    {
        RHI::PipelineHandle Reset{};
        RHI::PipelineHandle Assign{};
        RHI::PipelineHandle Update{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Reset.IsValid() && Assign.IsValid() && Update.IsValid();
        }
    };

    // A single packed "Work" buffer holds every compute-touched role as a
    // sub-span (see ComputeKMeansGpuBufferLayout); State is the BDA pointer
    // table; the readback buffers are host-visible drain targets used by a later
    // execution slice.
    struct KMeansGpuResourceSet
    {
        RHI::BufferHandle State{};
        RHI::BufferHandle Work{};
        RHI::BufferHandle LabelsReadback{};
        RHI::BufferHandle SquaredDistancesReadback{};
        RHI::BufferHandle CentroidsReadback{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return State.IsValid() && Work.IsValid();
        }
    };

    struct KMeansGpuRecordDesc
    {
        RHI::IDevice* Device{nullptr};
        RHI::ICommandContext* CommandContext{nullptr};
        KMeansGpuPipelineSet Pipelines{};
        KMeansGpuResourceSet Resources{};
        KMeansGpuPlanDesc Plan{};
    };

    struct KMeansGpuRecordResult
    {
        KMeansGpuStatus Status{KMeansGpuStatus::Success};
        bool Recorded{false};
        bool CpuFallbackRecommended{true};
        bool StateRecordUploaded{false};
        std::uint32_t MethodDispatchCount{0u};
        KMeansGpuDispatchPlan Plan{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == KMeansGpuStatus::Success;
        }
    };

    // Build the BDA pointer table for the compute-touched roles. Each pointer is
    // the packed Work buffer's device address plus the role's span offset; the
    // State buffer carries this record. Returns an all-zero record (which the
    // shaders treat as "skip") when the device lacks Buffer Device Address
    // support or the layout is invalid.
    [[nodiscard]] KMeansGpuStateBufferRecord BuildKMeansGpuStateRecord(
        RHI::IDevice& device,
        const KMeansGpuResourceSet& resources,
        const KMeansGpuBufferLayout& layout) noexcept;

    // Record the reset/assign/update dispatches for every planned iteration,
    // chained by ShaderWrite -> ShaderRead|ShaderWrite barriers on the Work
    // buffer. Fail-closed: reports MissingDevice / DeviceUnavailable /
    // InvalidGpuResource / plan status and recommends CPU fallback without
    // recording. The recorded command stream still leaves the results resident;
    // the host drain is a later execution slice.
    [[nodiscard]] KMeansGpuRecordResult RecordKMeansGpuPasses(
        const KMeansGpuRecordDesc& desc);
}
