module;

#include <cstdint>
#include <span>

export module Extrinsic.Graphics.ComputeParallelPrimitives;

import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;

export namespace Extrinsic::Graphics
{
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

    [[nodiscard]] ParallelPrimitiveCpuResult ComputePrefixScanCpu(
        std::span<const std::uint32_t> values,
        std::span<std::uint32_t> output,
        PrefixScanMode mode) noexcept;

    [[nodiscard]] ParallelPrimitiveCpuResult CompactByFlagsCpu(
        std::span<const std::uint32_t> keys,
        std::span<const std::uint32_t> flags,
        std::span<std::uint32_t> outputKeys) noexcept;

    [[nodiscard]] GpuParallelPrimitiveRecordResult RecordGpuPrefixScan(
        const GpuPrefixScanRecordDesc& desc) noexcept;

    [[nodiscard]] GpuParallelPrimitiveRecordResult RecordGpuStreamCompaction(
        const GpuStreamCompactionRecordDesc& desc) noexcept;
}
