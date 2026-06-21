module;

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

module Extrinsic.RHI.BufferTransfer;

namespace Extrinsic::RHI
{
    namespace
    {
        [[nodiscard]] constexpr bool ValidAlignment(const std::uint64_t alignment) noexcept
        {
            return alignment != 0u;
        }

        [[nodiscard]] constexpr bool AddWouldOverflow(const std::uint64_t lhs,
                                                      const std::uint64_t rhs) noexcept
        {
            return lhs > (std::numeric_limits<std::uint64_t>::max() - rhs);
        }

        [[nodiscard]] Core::Expected<std::uint64_t> CheckedAdd(const std::uint64_t lhs,
                                                               const std::uint64_t rhs) noexcept
        {
            if (AddWouldOverflow(lhs, rhs))
            {
                return Core::Err<std::uint64_t>(Core::ErrorCode::OutOfRange);
            }
            return Core::Ok(lhs + rhs);
        }

        [[nodiscard]] bool ShouldMerge(const BufferRange current,
                                       const BufferRange next,
                                       const BufferPartialWriteCoalesceMode mode) noexcept
        {
            if (mode == BufferPartialWriteCoalesceMode::None)
            {
                return false;
            }

            const std::uint64_t currentEnd = current.OffsetBytes + current.SizeBytes;
            if (mode == BufferPartialWriteCoalesceMode::Overlapping)
            {
                return next.OffsetBytes < currentEnd;
            }
            return next.OffsetBytes <= currentEnd;
        }

        [[nodiscard]] Core::Expected<std::uint64_t> ComputeDimensionFootprint(
            const std::uint64_t elementCount,
            const std::uint64_t componentBytes,
            const std::uint64_t effectiveStride) noexcept
        {
            if (elementCount == 0u)
            {
                return Core::Ok(std::uint64_t{0});
            }

            const std::uint64_t lastElement = elementCount - 1u;
            if (lastElement != 0u
                && effectiveStride > std::numeric_limits<std::uint64_t>::max() / lastElement)
            {
                return Core::Err<std::uint64_t>(Core::ErrorCode::OutOfRange);
            }

            const std::uint64_t lastOffset = lastElement * effectiveStride;
            return CheckedAdd(lastOffset, componentBytes);
        }

        [[nodiscard]] Core::Expected<std::uint64_t> ComputeTightBytes(
            const std::uint64_t elementCount,
            const std::uint64_t componentBytes) noexcept
        {
            if (elementCount != 0u
                && componentBytes > std::numeric_limits<std::uint64_t>::max() / elementCount)
            {
                return Core::Err<std::uint64_t>(Core::ErrorCode::OutOfRange);
            }
            return Core::Ok(elementCount * componentBytes);
        }
    } // namespace

    Core::Expected<BufferRange> ValidateBufferRange(const BufferDesc& desc,
                                                    const BufferRange range) noexcept
    {
        if (desc.SizeBytes == 0u || range.SizeBytes == 0u)
        {
            return Core::Err<BufferRange>(Core::ErrorCode::InvalidArgument);
        }
        if (range.OffsetBytes >= desc.SizeBytes)
        {
            return Core::Err<BufferRange>(Core::ErrorCode::OutOfRange);
        }
        if (range.SizeBytes > (desc.SizeBytes - range.OffsetBytes))
        {
            return Core::Err<BufferRange>(Core::ErrorCode::OutOfRange);
        }
        return Core::Expected<BufferRange>(range);
    }

    Core::Expected<std::uint64_t> AlignOffsetDown(const std::uint64_t value,
                                                  const std::uint64_t alignmentBytes) noexcept
    {
        if (!ValidAlignment(alignmentBytes))
        {
            return Core::Err<std::uint64_t>(Core::ErrorCode::InvalidArgument);
        }
        return Core::Ok((value / alignmentBytes) * alignmentBytes);
    }

    Core::Expected<std::uint64_t> AlignOffsetUp(const std::uint64_t value,
                                                const std::uint64_t alignmentBytes) noexcept
    {
        if (!ValidAlignment(alignmentBytes))
        {
            return Core::Err<std::uint64_t>(Core::ErrorCode::InvalidArgument);
        }
        const std::uint64_t remainder = value % alignmentBytes;
        if (remainder == 0u)
        {
            return Core::Expected<std::uint64_t>(value);
        }
        return CheckedAdd(value, alignmentBytes - remainder);
    }

    Core::Expected<BufferRange> ExpandBufferRangeToAlignment(
        const BufferDesc& desc,
        const BufferRange range,
        const BufferTransferAlignment& alignment) noexcept
    {
        if (!ValidAlignment(alignment.DestinationOffsetAlignmentBytes)
            || !ValidAlignment(alignment.SizeAlignmentBytes))
        {
            return Core::Err<BufferRange>(Core::ErrorCode::InvalidArgument);
        }

        auto validated = ValidateBufferRange(desc, range);
        if (!validated.has_value())
        {
            return Core::Err<BufferRange>(validated.error());
        }

        auto alignedStart = AlignOffsetDown(range.OffsetBytes, alignment.DestinationOffsetAlignmentBytes);
        if (!alignedStart.has_value())
        {
            return Core::Err<BufferRange>(alignedStart.error());
        }

        auto originalEnd = CheckedAdd(range.OffsetBytes, range.SizeBytes);
        if (!originalEnd.has_value())
        {
            return Core::Err<BufferRange>(originalEnd.error());
        }

        const std::uint64_t span = *originalEnd - *alignedStart;
        auto alignedSize = AlignOffsetUp(span, alignment.SizeAlignmentBytes);
        if (!alignedSize.has_value())
        {
            return Core::Err<BufferRange>(alignedSize.error());
        }

        auto alignedEnd = CheckedAdd(*alignedStart, *alignedSize);
        if (!alignedEnd.has_value() || *alignedEnd > desc.SizeBytes)
        {
            return Core::Err<BufferRange>(Core::ErrorCode::OutOfRange);
        }

        return ValidateBufferRange(desc, BufferRange{.OffsetBytes = *alignedStart,
                                                     .SizeBytes = *alignedSize});
    }

    Core::Expected<BufferPartialWritePlan> PlanPartialBufferWrites(
        const BufferDesc& desc,
        const std::span<const BufferRange> dirtyRanges,
        const BufferPartialWritePlanOptions& options) noexcept
    {
        if (!ValidAlignment(options.Alignment.SourceOffsetAlignmentBytes)
            || !ValidAlignment(options.Alignment.DestinationOffsetAlignmentBytes)
            || !ValidAlignment(options.Alignment.SizeAlignmentBytes))
        {
            return Core::Err<BufferPartialWritePlan>(Core::ErrorCode::InvalidArgument);
        }
        if (desc.SizeBytes == 0u)
        {
            return Core::Err<BufferPartialWritePlan>(Core::ErrorCode::InvalidArgument);
        }

        std::vector<BufferRange> ranges;
        ranges.reserve(dirtyRanges.size());
        for (const BufferRange dirty : dirtyRanges)
        {
            Core::Expected<BufferRange> range = options.AlignDestinationRanges
                ? ExpandBufferRangeToAlignment(desc, dirty, options.Alignment)
                : ValidateBufferRange(desc, dirty);
            if (!range.has_value())
            {
                return Core::Err<BufferPartialWritePlan>(range.error());
            }
            ranges.push_back(*range);
        }

        std::sort(ranges.begin(),
                  ranges.end(),
                  [](const BufferRange lhs, const BufferRange rhs)
                  {
                      if (lhs.OffsetBytes != rhs.OffsetBytes)
                      {
                          return lhs.OffsetBytes < rhs.OffsetBytes;
                      }
                      return lhs.SizeBytes < rhs.SizeBytes;
                  });

        std::vector<BufferRange> coalesced;
        coalesced.reserve(ranges.size());
        for (const BufferRange range : ranges)
        {
            if (coalesced.empty() || !ShouldMerge(coalesced.back(), range, options.Coalesce))
            {
                coalesced.push_back(range);
                continue;
            }

            BufferRange& current = coalesced.back();
            const std::uint64_t currentEnd = current.OffsetBytes + current.SizeBytes;
            const std::uint64_t nextEnd = range.OffsetBytes + range.SizeBytes;
            if (nextEnd > currentEnd)
            {
                current.SizeBytes = nextEnd - current.OffsetBytes;
            }
        }

        BufferPartialWritePlan plan{};
        plan.Regions.reserve(coalesced.size());

        std::uint64_t sourceCursor = 0u;
        for (const BufferRange range : coalesced)
        {
            auto sourceOffset = AlignOffsetUp(sourceCursor, options.Alignment.SourceOffsetAlignmentBytes);
            if (!sourceOffset.has_value())
            {
                return Core::Err<BufferPartialWritePlan>(sourceOffset.error());
            }

            auto sourceEnd = CheckedAdd(*sourceOffset, range.SizeBytes);
            if (!sourceEnd.has_value())
            {
                return Core::Err<BufferPartialWritePlan>(sourceEnd.error());
            }

            auto destinationTotal = CheckedAdd(plan.TotalDestinationBytes, range.SizeBytes);
            if (!destinationTotal.has_value())
            {
                return Core::Err<BufferPartialWritePlan>(destinationTotal.error());
            }

            plan.Regions.push_back(BufferCopyRegion{
                .SourceOffsetBytes = *sourceOffset,
                .DestinationOffsetBytes = range.OffsetBytes,
                .SizeBytes = range.SizeBytes,
            });
            sourceCursor = *sourceEnd;
            plan.TotalDestinationBytes = *destinationTotal;
        }

        plan.TotalSourceBytes = sourceCursor;
        return Core::Ok(std::move(plan));
    }

    Core::Expected<BufferDimensionMatch> ValidateBufferDimensions(
        const BufferDimensionMatchDesc& desc) noexcept
    {
        if (desc.ComponentBytes == 0u)
        {
            return Core::Err<BufferDimensionMatch>(Core::ErrorCode::InvalidArgument);
        }
        if (AddWouldOverflow(desc.Region.OffsetBytes, desc.Region.SizeBytes))
        {
            return Core::Err<BufferDimensionMatch>(Core::ErrorCode::OutOfRange);
        }

        const std::uint64_t effectiveStride = desc.StrideBytes == 0u
            ? desc.ComponentBytes
            : desc.StrideBytes;
        if (effectiveStride < desc.ComponentBytes)
        {
            return Core::Err<BufferDimensionMatch>(Core::ErrorCode::InvalidArgument);
        }

        auto tightBytes = ComputeTightBytes(desc.ElementCount, desc.ComponentBytes);
        if (!tightBytes.has_value())
        {
            return Core::Err<BufferDimensionMatch>(tightBytes.error());
        }

        auto footprint = ComputeDimensionFootprint(desc.ElementCount,
                                                   desc.ComponentBytes,
                                                   effectiveStride);
        if (!footprint.has_value())
        {
            return Core::Err<BufferDimensionMatch>(footprint.error());
        }

        const bool exact = *footprint == desc.Region.SizeBytes;
        if (desc.Mode == BufferDimensionMatchMode::Exact && !exact)
        {
            return Core::Err<BufferDimensionMatch>(Core::ErrorCode::TypeMismatch);
        }
        if (desc.Mode == BufferDimensionMatchMode::FitWithin && *footprint > desc.Region.SizeBytes)
        {
            return Core::Err<BufferDimensionMatch>(Core::ErrorCode::TypeMismatch);
        }

        return Core::Ok(BufferDimensionMatch{
            .Region = desc.Region,
            .ElementCount = desc.ElementCount,
            .ComponentBytes = desc.ComponentBytes,
            .EffectiveStrideBytes = effectiveStride,
            .TightBytes = *tightBytes,
            .FootprintBytes = *footprint,
            .MatchesExactly = exact,
        });
    }
} // namespace Extrinsic::RHI
