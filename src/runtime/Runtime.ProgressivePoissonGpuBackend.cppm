module;

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.ProgressivePoissonGpuBackend;

import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;

export namespace Extrinsic::Runtime
{
    inline constexpr std::uint32_t kProgressivePoissonGpuGroupSize = 256u;

    enum class ProgressivePoissonGpuStatus : std::uint8_t
    {
        Success,
        InvalidInput,
        MissingDevice,
        DeviceUnavailable,
        PlanningOnly,
        SizeOverflow,
        InvalidGpuResource,
        InvalidReadback,
        ParityMismatch,
    };

    enum class ProgressivePoissonGpuPassKind : std::uint8_t
    {
        BuildLevelCells,
        AcceptPhase,
        CompactAccepted,
        CompactRemaining,
    };

    enum class ProgressivePoissonGpuBufferRole : std::uint8_t
    {
        None,
        State,
        PositionX,
        PositionY,
        PositionZ,
        RemainingKeys,
        NextRemainingKeys,
        AcceptedKeys,
        CellKeys,
        CellPhases,
        AcceptFlags,
        CarryFlags,
        HashKeys,
        HashValues,
        LevelOffsets,
        SplatRadii,
        OutputCount,
        CompactionScratch,
    };

    struct ProgressivePoissonGpuConfig
    {
        std::uint32_t Dimension{3u};
        std::uint32_t GridWidth{4u};
        std::uint32_t MaxLevels{16u};
        float HashLoadFactor{0.25f};
        float RadiusAlpha{-1.0f};
        bool RandomizeGridOrigin{true};
        std::uint32_t GridOriginSeed{1337u};
        bool ShuffleWithinLevels{true};
        std::uint32_t ShuffleSeed{0x51ed270bu};
    };

    struct ProgressivePoissonGpuPlanDesc
    {
        std::uint32_t InputCount{0u};
        ProgressivePoissonGpuConfig Config{};
        std::uint32_t GroupSize{kProgressivePoissonGpuGroupSize};
    };

    struct ProgressivePoissonGpuBufferSpan
    {
        ProgressivePoissonGpuBufferRole Role{ProgressivePoissonGpuBufferRole::None};
        std::uint64_t OffsetBytes{0u};
        std::uint64_t SizeBytes{0u};
    };

    struct ProgressivePoissonGpuBufferLayout
    {
        std::uint32_t InputCount{0u};
        std::uint32_t MaxLevels{0u};
        std::uint32_t HashTableCapacity{0u};
        std::uint64_t StateBytes{0u};
        std::uint64_t WorkBufferBytes{0u};
        std::uint64_t CompactionScratchBytes{0u};
        std::vector<ProgressivePoissonGpuBufferSpan> Spans{};
    };

    struct ProgressivePoissonGpuDispatchDesc
    {
        ProgressivePoissonGpuPassKind Kind{
            ProgressivePoissonGpuPassKind::BuildLevelCells};
        std::uint32_t LevelIndex{0u};
        std::uint32_t PhaseIndex{0u};
        std::uint32_t ElementCount{0u};
        std::uint32_t HashTableCapacity{0u};
        std::uint32_t GroupSize{kProgressivePoissonGpuGroupSize};
        std::uint32_t GroupCountX{0u};
        std::uint32_t GroupCountY{1u};
        std::uint32_t GroupCountZ{1u};
    };

    struct ProgressivePoissonGpuLevelPlan
    {
        std::uint32_t LevelIndex{0u};
        std::uint32_t PhaseCount{0u};
        std::uint32_t RemainingUpperBound{0u};
        std::uint32_t HashTableCapacity{0u};
        ProgressivePoissonGpuDispatchDesc BuildCells{};
        std::vector<ProgressivePoissonGpuDispatchDesc> AcceptPhases{};
        Extrinsic::Graphics::ParallelPrimitiveDispatchPlan AcceptedCompaction{};
        Extrinsic::Graphics::ParallelPrimitiveDispatchPlan RemainingCompaction{};
    };

    struct ProgressivePoissonGpuDispatchPlan
    {
        ProgressivePoissonGpuStatus Status{ProgressivePoissonGpuStatus::Success};
        std::uint32_t InputCount{0u};
        std::uint32_t Dimension{0u};
        std::uint32_t PhaseCount{0u};
        std::uint32_t GroupSize{kProgressivePoissonGpuGroupSize};
        ProgressivePoissonGpuBufferLayout Layout{};
        std::vector<ProgressivePoissonGpuLevelPlan> Levels{};
        std::vector<ProgressivePoissonGpuDispatchDesc> Dispatches{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Status == ProgressivePoissonGpuStatus::Success;
        }
    };

    // Matches the BDA table consumed by the planned progressive Poisson shaders.
    struct ProgressivePoissonGpuStateBufferRecord
    {
        std::uint64_t PositionXBDA{0u};
        std::uint64_t PositionYBDA{0u};
        std::uint64_t PositionZBDA{0u};
        std::uint64_t RemainingKeysBDA{0u};
        std::uint64_t NextRemainingKeysBDA{0u};
        std::uint64_t AcceptedKeysBDA{0u};
        std::uint64_t CellKeysBDA{0u};
        std::uint64_t CellPhasesBDA{0u};
        std::uint64_t AcceptFlagsBDA{0u};
        std::uint64_t CarryFlagsBDA{0u};
        std::uint64_t HashKeysBDA{0u};
        std::uint64_t HashValuesBDA{0u};
        std::uint64_t LevelOffsetsBDA{0u};
        std::uint64_t SplatRadiiBDA{0u};
        std::uint64_t OutputCountBDA{0u};
        std::uint64_t CompactionScratchBDA{0u};
    };
    static_assert(sizeof(ProgressivePoissonGpuStateBufferRecord) == 128u);

    // Matches `assets/shaders/progressive_poisson_*.comp` scalar push layout.
    struct ProgressivePoissonGpuPassPushConstants
    {
        std::uint64_t StateBDA{0u};
        std::uint32_t InputCount{0u};
        std::uint32_t RemainingCount{0u};
        std::uint32_t HashTableCapacity{0u};
        std::uint32_t Dimension{3u};
        std::uint32_t GridWidth{4u};
        std::uint32_t LevelIndex{0u};
        std::uint32_t PhaseIndex{0u};
        std::uint32_t PhaseCount{0u};
        float InvCellSize{0.0f};
        float RadiusSquared{0.0f};
        float OriginX{0.0f};
        float OriginY{0.0f};
        float OriginZ{0.0f};
        float Reserved0{0.0f};
    };
    static_assert(sizeof(ProgressivePoissonGpuPassPushConstants) == 64u);

    struct ProgressivePoissonGpuResolveDesc
    {
        RHI::IDevice* Device{nullptr};
        ProgressivePoissonGpuPlanDesc Plan{};
    };

    struct ProgressivePoissonGpuResolveResult
    {
        ProgressivePoissonGpuStatus Status{ProgressivePoissonGpuStatus::Success};
        bool GpuExecutionAvailable{false};
        bool CpuFallbackRecommended{false};
        std::string Diagnostic{};
        ProgressivePoissonGpuDispatchPlan Plan{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == ProgressivePoissonGpuStatus::Success;
        }
    };

    struct ProgressivePoissonGpuPipelineSet
    {
        RHI::PipelineHandle BuildCells{};
        RHI::PipelineHandle AcceptPhase{};
        Extrinsic::Graphics::ParallelPrimitivePipelineSet Compaction{};
    };

    struct ProgressivePoissonGpuResourceSet
    {
        RHI::BufferHandle State{};
        RHI::BufferHandle PositionX{};
        RHI::BufferHandle PositionY{};
        RHI::BufferHandle PositionZ{};
        RHI::BufferHandle RemainingKeys{};
        RHI::BufferHandle NextRemainingKeys{};
        RHI::BufferHandle AcceptedKeys{};
        RHI::BufferHandle CellKeys{};
        RHI::BufferHandle CellPhases{};
        RHI::BufferHandle AcceptFlags{};
        RHI::BufferHandle CarryFlags{};
        RHI::BufferHandle HashKeys{};
        RHI::BufferHandle HashValues{};
        RHI::BufferHandle LevelOffsets{};
        RHI::BufferHandle SplatRadii{};
        RHI::BufferHandle OutputCount{};
        RHI::BufferHandle CompactionScratch{};
    };

    struct ProgressivePoissonGpuRecordDesc
    {
        RHI::IDevice* Device{nullptr};
        RHI::ICommandContext* CommandContext{nullptr};
        RHI::BufferManager* Buffers{nullptr};
        ProgressivePoissonGpuPipelineSet Pipelines{};
        ProgressivePoissonGpuResourceSet Resources{};
        ProgressivePoissonGpuPlanDesc Plan{};
    };

    struct ProgressivePoissonGpuRecordResult
    {
        ProgressivePoissonGpuStatus Status{ProgressivePoissonGpuStatus::Success};
        bool Recorded{false};
        bool CpuFallbackRecommended{false};
        bool StateRecordUploaded{false};
        std::uint32_t MethodDispatchCount{0u};
        std::uint32_t AcceptedCompactionCount{0u};
        std::uint32_t RemainingCompactionCount{0u};
        Extrinsic::Graphics::ParallelPrimitiveStatus FirstCompactionFailure{
            Extrinsic::Graphics::ParallelPrimitiveStatus::Success};
        ProgressivePoissonGpuDispatchPlan Plan{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == ProgressivePoissonGpuStatus::Success;
        }
    };

    struct ProgressivePoissonGpuExecutionResources
    {
        ProgressivePoissonGpuResourceSet Resources{};
        RHI::BufferHandle OrderReadback{};
        RHI::BufferHandle LevelOffsetsReadback{};
        RHI::BufferHandle SplatRadiiReadback{};
        std::vector<RHI::BufferManager::BufferLease> Leases{};

        [[nodiscard]] bool HasReadbackTargets() const noexcept
        {
            return OrderReadback.IsValid() &&
                   LevelOffsetsReadback.IsValid() &&
                   SplatRadiiReadback.IsValid();
        }
    };

    struct ProgressivePoissonGpuExecutionDesc
    {
        RHI::IDevice* Device{nullptr};
        RHI::ICommandContext* CommandContext{nullptr};
        RHI::BufferManager* Buffers{nullptr};
        ProgressivePoissonGpuPipelineSet Pipelines{};
        std::span<const glm::vec3> Positions{};
        ProgressivePoissonGpuPlanDesc Plan{};
    };

    struct ProgressivePoissonGpuExecutionResult
    {
        ProgressivePoissonGpuStatus Status{ProgressivePoissonGpuStatus::Success};
        bool Recorded{false};
        bool CpuFallbackRecommended{true};
        bool UploadedInputs{false};
        bool ReadbackCopiesRecorded{false};
        std::uint32_t UploadWriteCount{0u};
        std::uint32_t ReadbackCopyCount{0u};
        ProgressivePoissonGpuDispatchPlan Plan{};
        ProgressivePoissonGpuRecordResult Record{};
        ProgressivePoissonGpuExecutionResources Resources{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == ProgressivePoissonGpuStatus::Success;
        }
    };

    struct ProgressivePoissonGpuReadbackDesc
    {
        RHI::IDevice* Device{nullptr};
        RHI::BufferHandle OrderReadback{};
        RHI::BufferHandle LevelOffsetsReadback{};
        RHI::BufferHandle SplatRadiiReadback{};
        ProgressivePoissonGpuDispatchPlan Plan{};
    };

    struct ProgressivePoissonGpuReadbackResult
    {
        ProgressivePoissonGpuStatus Status{ProgressivePoissonGpuStatus::Success};
        bool Read{false};
        bool StructurallyValid{false};
        bool CpuFallbackRecommended{true};
        std::uint32_t AcceptedCount{0u};
        std::vector<std::uint32_t> Order{};
        std::vector<std::uint32_t> LevelOffsets{};
        std::vector<float> SplatRadii{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == ProgressivePoissonGpuStatus::Success;
        }
    };

    struct ProgressivePoissonGpuReferenceView
    {
        std::span<const std::uint32_t> Order{};
        std::span<const std::uint32_t> LevelOffsets{};
        std::span<const float> SplatRadii{};
        std::span<const float> LevelRadii{};
    };

    struct ProgressivePoissonGpuParityDesc
    {
        std::span<const glm::vec3> Positions{};
        std::span<const std::uint32_t> GpuOrder{};
        std::span<const std::uint32_t> GpuLevelOffsets{};
        std::span<const float> GpuSplatRadii{};
        ProgressivePoissonGpuReferenceView Reference{};
        std::uint32_t Dimension{3u};
        float SplatRadiusTolerance{1.0e-5f};
        float PoissonRatioTolerance{1.0e-5f};
    };

    struct ProgressivePoissonGpuParityDiagnostics
    {
        ProgressivePoissonGpuStatus Status{ProgressivePoissonGpuStatus::Success};
        bool Compared{false};
        bool MatchesReference{false};
        bool OrderMatches{false};
        bool LevelOffsetsMatch{false};
        bool SplatRadiiWithinTolerance{false};
        bool PoissonGuaranteeHolds{false};
        bool CpuFallbackRecommended{true};
        std::uint32_t GpuAcceptedCount{0u};
        std::uint32_t ReferenceAcceptedCount{0u};
        std::uint32_t OrderMismatchCount{0u};
        std::uint32_t LevelOffsetMismatchCount{0u};
        std::uint32_t SplatRadiusMismatchCount{0u};
        float MaxSplatRadiusAbsDelta{0.0f};
        float MinPoissonRatio{0.0f};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == ProgressivePoissonGpuStatus::Success;
        }
    };

    [[nodiscard]] const char* DebugNameForProgressivePoissonGpuStatus(
        ProgressivePoissonGpuStatus status) noexcept;

    [[nodiscard]] const char* DebugNameForProgressivePoissonGpuPassKind(
        ProgressivePoissonGpuPassKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForProgressivePoissonGpuBufferRole(
        ProgressivePoissonGpuBufferRole role) noexcept;

    [[nodiscard]] ProgressivePoissonGpuDispatchPlan
    ComputeProgressivePoissonGpuDispatchPlan(
        const ProgressivePoissonGpuPlanDesc& desc);

    [[nodiscard]] RHI::BufferDesc BuildProgressivePoissonGpuStateBufferDesc(
        const char* debugName = "ProgressivePoissonGpu.State") noexcept;

    [[nodiscard]] RHI::BufferDesc BuildProgressivePoissonGpuWorkBufferDesc(
        const ProgressivePoissonGpuBufferLayout& layout,
        const char* debugName = "ProgressivePoissonGpu.Work") noexcept;

    [[nodiscard]] RHI::BufferDesc BuildProgressivePoissonGpuReadbackBufferDesc(
        std::uint64_t sizeBytes,
        const char* debugName = "ProgressivePoissonGpu.Readback") noexcept;

    [[nodiscard]] RHI::PipelineDesc BuildProgressivePoissonBuildCellsPipelineDesc(
        const char* shaderPath =
            "shaders/progressive_poisson_build_cells.comp.spv");

    [[nodiscard]] RHI::PipelineDesc BuildProgressivePoissonAcceptPhasePipelineDesc(
        const char* shaderPath =
            "shaders/progressive_poisson_accept_phase.comp.spv");

    [[nodiscard]] ProgressivePoissonGpuResolveResult
    ResolveProgressivePoissonGpuRequest(
        const ProgressivePoissonGpuResolveDesc& desc);

    [[nodiscard]] ProgressivePoissonGpuStateBufferRecord
    BuildProgressivePoissonGpuStateRecord(
        RHI::IDevice& device,
        const ProgressivePoissonGpuResourceSet& resources) noexcept;

    [[nodiscard]] ProgressivePoissonGpuRecordResult
    RecordProgressivePoissonGpuPasses(
        const ProgressivePoissonGpuRecordDesc& desc);

    [[nodiscard]] ProgressivePoissonGpuExecutionResult
    RecordProgressivePoissonGpuExecution(
        const ProgressivePoissonGpuExecutionDesc& desc);

    [[nodiscard]] ProgressivePoissonGpuReadbackResult
    ReadProgressivePoissonGpuReadbacks(
        const ProgressivePoissonGpuReadbackDesc& desc);

    [[nodiscard]] ProgressivePoissonGpuParityDiagnostics
    CompareProgressivePoissonGpuOutputToReference(
        const ProgressivePoissonGpuParityDesc& desc);
}
