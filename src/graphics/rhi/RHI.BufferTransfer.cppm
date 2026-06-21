module;

#include <cstdint>
#include <span>
#include <vector>

export module Extrinsic.RHI.BufferTransfer;

import Extrinsic.Core.Error;
import Extrinsic.RHI.Descriptors;

export namespace Extrinsic::RHI
{
    struct BufferRange
    {
        std::uint64_t OffsetBytes{0};
        std::uint64_t SizeBytes{0};
    };

    struct BufferCopyRegion
    {
        std::uint64_t SourceOffsetBytes{0};
        std::uint64_t DestinationOffsetBytes{0};
        std::uint64_t SizeBytes{0};
    };

    struct BufferTransferAlignment
    {
        std::uint64_t SourceOffsetAlignmentBytes{1};
        std::uint64_t DestinationOffsetAlignmentBytes{1};
        std::uint64_t SizeAlignmentBytes{1};
    };

    enum class BufferPartialWriteCoalesceMode : std::uint8_t
    {
        None,
        Overlapping,
        OverlappingOrAdjacent,
    };

    struct BufferPartialWritePlanOptions
    {
        BufferTransferAlignment Alignment{};
        BufferPartialWriteCoalesceMode Coalesce{BufferPartialWriteCoalesceMode::OverlappingOrAdjacent};
        bool AlignDestinationRanges{true};
    };

    struct BufferPartialWritePlan
    {
        std::vector<BufferCopyRegion> Regions{};
        std::uint64_t TotalSourceBytes{0};
        std::uint64_t TotalDestinationBytes{0};
    };

    enum class BufferDimensionMatchMode : std::uint8_t
    {
        Exact,
        FitWithin,
    };

    struct BufferDimensionMatchDesc
    {
        std::uint64_t ElementCount{0};
        std::uint64_t ComponentBytes{0};
        BufferRange Region{};
        std::uint64_t StrideBytes{0}; // 0 means tightly packed.
        BufferDimensionMatchMode Mode{BufferDimensionMatchMode::Exact};
    };

    struct BufferDimensionMatch
    {
        BufferRange Region{};
        std::uint64_t ElementCount{0};
        std::uint64_t ComponentBytes{0};
        std::uint64_t EffectiveStrideBytes{0};
        std::uint64_t TightBytes{0};
        std::uint64_t FootprintBytes{0};
        bool MatchesExactly{false};
    };

    [[nodiscard]] Core::Expected<BufferRange> ValidateBufferRange(
        const BufferDesc& desc,
        BufferRange range) noexcept;

    [[nodiscard]] Core::Expected<std::uint64_t> AlignOffsetDown(
        std::uint64_t value,
        std::uint64_t alignmentBytes) noexcept;

    [[nodiscard]] Core::Expected<std::uint64_t> AlignOffsetUp(
        std::uint64_t value,
        std::uint64_t alignmentBytes) noexcept;

    [[nodiscard]] Core::Expected<BufferRange> ExpandBufferRangeToAlignment(
        const BufferDesc& desc,
        BufferRange range,
        const BufferTransferAlignment& alignment) noexcept;

    [[nodiscard]] Core::Expected<BufferPartialWritePlan> PlanPartialBufferWrites(
        const BufferDesc& desc,
        std::span<const BufferRange> dirtyRanges,
        const BufferPartialWritePlanOptions& options = {}) noexcept;

    [[nodiscard]] Core::Expected<BufferDimensionMatch> ValidateBufferDimensions(
        const BufferDimensionMatchDesc& desc) noexcept;
} // namespace Extrinsic::RHI
