module;

#include <cstdint>
#include <span>
#include <vector>

export module Extrinsic.Graphics.ComputeParallelPrimitives;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;

export namespace Extrinsic::Graphics
{
    inline constexpr std::uint32_t kParallelPrimitiveGroupSize = 256u;
    inline constexpr std::uint64_t kParallelPrimitiveInvalidOffset = ~std::uint64_t{0u};

    enum class ParallelPrimitiveStatus : std::uint8_t
    {
        Success,
        InvalidInput,
        SizeMismatch,
        OutputTooSmall,
        SumOverflow,
        DeviceUnavailable,
        InvalidGpuResource,
        UnsupportedInCurrentSlice,
    };

    enum class PrefixScanMode : std::uint8_t
    {
        Exclusive,
        Inclusive,
    };

    enum class ParallelPrimitiveKind : std::uint8_t
    {
        PrefixScan,
        StreamCompaction,
    };

    enum class ParallelPrimitivePassKind : std::uint8_t
    {
        PrefixBlockScan,
        PrefixAddBlockOffsets,
        StreamCompactScatter,
    };

    enum class ParallelPrimitiveBufferRole : std::uint8_t
    {
        None,
        Input,
        Output,
        Keys,
        Flags,
        OutputKeys,
        OutputCount,
        Scratch,
    };

    struct ParallelPrimitiveDiagnostics
    {
        std::uint32_t ElementCount = 0u;
        std::uint32_t OutputCount = 0u;
        std::uint32_t FirstFailureIndex = 0u;
        std::uint64_t Total = 0u;
    };

    struct ParallelPrimitiveCpuResult
    {
        ParallelPrimitiveStatus Status = ParallelPrimitiveStatus::Success;
        ParallelPrimitiveDiagnostics Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == ParallelPrimitiveStatus::Success;
        }
    };

    struct ParallelPrimitiveScratchLevel
    {
        std::uint32_t LevelIndex = 0u;
        std::uint32_t ElementCount = 0u;
        std::uint32_t BlockCount = 0u;
        std::uint64_t OffsetBytes = 0u;
        std::uint64_t SizeBytes = 0u;
    };

    struct ParallelPrimitiveDispatchDesc
    {
        ParallelPrimitivePassKind Kind = ParallelPrimitivePassKind::PrefixBlockScan;
        PrefixScanMode Mode = PrefixScanMode::Exclusive;
        std::uint32_t LevelIndex = 0u;
        std::uint32_t ElementCount = 0u;
        std::uint32_t GroupSize = kParallelPrimitiveGroupSize;
        std::uint32_t GroupCountX = 0u;
        std::uint32_t GroupCountY = 1u;
        std::uint32_t GroupCountZ = 1u;
        ParallelPrimitiveBufferRole InputRole = ParallelPrimitiveBufferRole::None;
        ParallelPrimitiveBufferRole OutputRole = ParallelPrimitiveBufferRole::None;
        ParallelPrimitiveBufferRole BlockSumsRole = ParallelPrimitiveBufferRole::None;
        ParallelPrimitiveBufferRole OffsetsRole = ParallelPrimitiveBufferRole::None;
        ParallelPrimitiveBufferRole CountRole = ParallelPrimitiveBufferRole::None;
        std::uint64_t InputOffsetBytes = kParallelPrimitiveInvalidOffset;
        std::uint64_t OutputOffsetBytes = kParallelPrimitiveInvalidOffset;
        std::uint64_t BlockSumsOffsetBytes = kParallelPrimitiveInvalidOffset;
        std::uint64_t OffsetsOffsetBytes = kParallelPrimitiveInvalidOffset;
        std::uint64_t CountOffsetBytes = kParallelPrimitiveInvalidOffset;
    };

    struct ParallelPrimitiveBarrierDesc
    {
        std::uint32_t AfterDispatchIndex = 0u;
        ParallelPrimitiveBufferRole Buffer = ParallelPrimitiveBufferRole::None;
        RHI::MemoryAccess Before = RHI::MemoryAccess::None;
        RHI::MemoryAccess After = RHI::MemoryAccess::None;
    };

    struct ParallelPrimitiveDispatchPlan
    {
        ParallelPrimitiveStatus Status = ParallelPrimitiveStatus::Success;
        ParallelPrimitiveKind Kind = ParallelPrimitiveKind::PrefixScan;
        PrefixScanMode Mode = PrefixScanMode::Exclusive;
        std::uint32_t ElementCount = 0u;
        std::uint32_t GroupSize = kParallelPrimitiveGroupSize;
        std::uint64_t ScratchBytes = 0u;
        std::uint64_t PrefixOffsetsOffsetBytes = kParallelPrimitiveInvalidOffset;
        std::uint64_t PrefixOffsetsSizeBytes = 0u;
        std::vector<ParallelPrimitiveScratchLevel> ScratchLevels{};
        std::vector<ParallelPrimitiveDispatchDesc> Dispatches{};
        std::vector<ParallelPrimitiveBarrierDesc> Barriers{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Status == ParallelPrimitiveStatus::Success;
        }
    };

    // Matches `assets/shaders/parallel_prefix_scan.comp` scalar push layout.
    struct ParallelPrefixScanPushConstants
    {
        std::uint64_t InputBDA = 0u;
        std::uint64_t OutputBDA = 0u;
        std::uint64_t BlockSumsBDA = 0u;
        std::uint32_t ElementCount = 0u;
        std::uint32_t Mode = 0u;
        std::uint32_t LevelIndex = 0u;
        std::uint32_t Reserved0 = 0u;
    };
    static_assert(sizeof(ParallelPrefixScanPushConstants) == 40u);

    // Matches `assets/shaders/parallel_scan_add_offsets.comp` scalar push layout.
    struct ParallelScanAddOffsetsPushConstants
    {
        std::uint64_t OutputBDA = 0u;
        std::uint64_t OffsetsBDA = 0u;
        std::uint32_t ElementCount = 0u;
        std::uint32_t Reserved0 = 0u;
        std::uint32_t Reserved1 = 0u;
        std::uint32_t Reserved2 = 0u;
    };
    static_assert(sizeof(ParallelScanAddOffsetsPushConstants) == 32u);

    // Matches `assets/shaders/parallel_compact_by_flags.comp` scalar push layout.
    struct ParallelCompactByFlagsPushConstants
    {
        std::uint64_t KeysBDA = 0u;
        std::uint64_t FlagsBDA = 0u;
        std::uint64_t OffsetsBDA = 0u;
        std::uint64_t OutputKeysBDA = 0u;
        std::uint64_t OutputCountBDA = 0u;
        std::uint32_t ElementCount = 0u;
        std::uint32_t Reserved0 = 0u;
        std::uint32_t Reserved1 = 0u;
        std::uint32_t Reserved2 = 0u;
    };
    static_assert(sizeof(ParallelCompactByFlagsPushConstants) == 56u);

    struct GpuPrefixScanRecordDesc
    {
        RHI::IDevice* Device = nullptr;
        RHI::BufferHandle Input{};
        RHI::BufferHandle Output{};
        RHI::BufferHandle Scratch{};
        std::uint32_t ElementCount = 0u;
        PrefixScanMode Mode = PrefixScanMode::Exclusive;
    };

    struct GpuStreamCompactionRecordDesc
    {
        RHI::IDevice* Device = nullptr;
        RHI::BufferHandle Keys{};
        RHI::BufferHandle Flags{};
        RHI::BufferHandle OutputKeys{};
        RHI::BufferHandle OutputCount{};
        RHI::BufferHandle Scratch{};
        std::uint32_t ElementCount = 0u;
    };

    struct GpuParallelPrimitiveRecordResult
    {
        ParallelPrimitiveStatus Status = ParallelPrimitiveStatus::Success;
        ParallelPrimitiveKind Kind = ParallelPrimitiveKind::PrefixScan;
        ParallelPrimitiveDiagnostics Diagnostics{};
        bool Recorded = false;
        bool CpuFallbackRecommended = false;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == ParallelPrimitiveStatus::Success;
        }
    };

    [[nodiscard]] const char* DebugNameForParallelPrimitiveStatus(
        ParallelPrimitiveStatus status) noexcept;

    [[nodiscard]] const char* DebugNameForParallelPrimitivePassKind(
        ParallelPrimitivePassKind kind) noexcept;

    [[nodiscard]] const char* DebugNameForParallelPrimitiveBufferRole(
        ParallelPrimitiveBufferRole role) noexcept;

    [[nodiscard]] ParallelPrimitiveCpuResult ComputePrefixScanCpu(
        std::span<const std::uint32_t> values,
        std::span<std::uint32_t> output,
        PrefixScanMode mode) noexcept;

    [[nodiscard]] ParallelPrimitiveCpuResult CompactByFlagsCpu(
        std::span<const std::uint32_t> keys,
        std::span<const std::uint32_t> flags,
        std::span<std::uint32_t> outputKeys) noexcept;

    [[nodiscard]] ParallelPrimitiveDispatchPlan ComputePrefixScanDispatchPlan(
        std::uint32_t elementCount,
        PrefixScanMode mode,
        std::uint32_t groupSize = kParallelPrimitiveGroupSize);

    [[nodiscard]] ParallelPrimitiveDispatchPlan ComputeStreamCompactionDispatchPlan(
        std::uint32_t elementCount,
        std::uint32_t groupSize = kParallelPrimitiveGroupSize);

    [[nodiscard]] RHI::BufferDesc BuildParallelPrimitiveScratchBufferDesc(
        const ParallelPrimitiveDispatchPlan& plan,
        const char* debugName = "ParallelPrimitives.Scratch") noexcept;

    [[nodiscard]] GpuParallelPrimitiveRecordResult RecordGpuPrefixScan(
        const GpuPrefixScanRecordDesc& desc) noexcept;

    [[nodiscard]] GpuParallelPrimitiveRecordResult RecordGpuStreamCompaction(
        const GpuStreamCompactionRecordDesc& desc) noexcept;
}
