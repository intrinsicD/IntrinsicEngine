#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>

import Extrinsic.Core.Error;
import Extrinsic.RHI.BufferTransfer;
import Extrinsic.RHI.Descriptors;

namespace
{
    namespace Core = Extrinsic::Core;
    namespace RHI = Extrinsic::RHI;

    [[nodiscard]] RHI::BufferDesc Buffer(const std::uint64_t size)
    {
        return RHI::BufferDesc{
            .SizeBytes = size,
            .Usage = RHI::BufferUsage::TransferDst | RHI::BufferUsage::TransferSrc,
            .HostVisible = false,
            .DebugName = "BufferTransferTest",
        };
    }
}

TEST(BufferTransferRange, ValidatesExactSubranges)
{
    const auto desc = Buffer(64u);

    const auto range = RHI::ValidateBufferRange(desc, RHI::BufferRange{.OffsetBytes = 16u, .SizeBytes = 16u});
    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(range->OffsetBytes, 16u);
    EXPECT_EQ(range->SizeBytes, 16u);

    const auto tail = RHI::ValidateBufferRange(desc, RHI::BufferRange{.OffsetBytes = 32u, .SizeBytes = 32u});
    ASSERT_TRUE(tail.has_value());
    EXPECT_EQ(tail->OffsetBytes + tail->SizeBytes, desc.SizeBytes);
}

TEST(BufferTransferRange, RejectsEmptyBuffersAndZeroByteRanges)
{
    EXPECT_EQ(RHI::ValidateBufferRange(Buffer(0u), RHI::BufferRange{.OffsetBytes = 0u, .SizeBytes = 1u}).error(),
              Core::ErrorCode::InvalidArgument);
    EXPECT_EQ(RHI::ValidateBufferRange(Buffer(64u), RHI::BufferRange{.OffsetBytes = 0u, .SizeBytes = 0u}).error(),
              Core::ErrorCode::InvalidArgument);
}

TEST(BufferTransferRange, RejectsOutOfBoundsWithoutOverflow)
{
    const auto desc = Buffer(64u);

    EXPECT_EQ(RHI::ValidateBufferRange(desc, RHI::BufferRange{.OffsetBytes = 64u, .SizeBytes = 1u}).error(),
              Core::ErrorCode::OutOfRange);
    EXPECT_EQ(RHI::ValidateBufferRange(desc, RHI::BufferRange{.OffsetBytes = 63u, .SizeBytes = 2u}).error(),
              Core::ErrorCode::OutOfRange);
    EXPECT_EQ(RHI::ValidateBufferRange(desc,
                                       RHI::BufferRange{
                                           .OffsetBytes = std::numeric_limits<std::uint64_t>::max(),
                                           .SizeBytes = 8u,
                                       }).error(),
              Core::ErrorCode::OutOfRange);
}

TEST(BufferTransferAlignment, AlignsWithNonPowerOfTwoValues)
{
    const auto down = RHI::AlignOffsetDown(37u, 12u);
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(*down, 36u);

    const auto up = RHI::AlignOffsetUp(37u, 12u);
    ASSERT_TRUE(up.has_value());
    EXPECT_EQ(*up, 48u);
}

TEST(BufferTransferAlignment, RejectsInvalidAndOverflowingAlignment)
{
    EXPECT_EQ(RHI::AlignOffsetDown(4u, 0u).error(), Core::ErrorCode::InvalidArgument);
    EXPECT_EQ(RHI::AlignOffsetUp(std::numeric_limits<std::uint64_t>::max() - 1u, 4u).error(),
              Core::ErrorCode::OutOfRange);
}

TEST(BufferTransferAlignment, ExpandsDestinationRangeAndRevalidatesCapacity)
{
    RHI::BufferTransferAlignment alignment;
    alignment.DestinationOffsetAlignmentBytes = 4u;
    alignment.SizeAlignmentBytes = 8u;

    const auto expanded = RHI::ExpandBufferRangeToAlignment(Buffer(64u),
                                                            RHI::BufferRange{.OffsetBytes = 5u, .SizeBytes = 7u},
                                                            alignment);
    ASSERT_TRUE(expanded.has_value());
    EXPECT_EQ(expanded->OffsetBytes, 4u);
    EXPECT_EQ(expanded->SizeBytes, 8u);

    const auto tail = RHI::ExpandBufferRangeToAlignment(Buffer(16u),
                                                        RHI::BufferRange{.OffsetBytes = 10u, .SizeBytes = 1u},
                                                        alignment);
    ASSERT_TRUE(tail.has_value());
    EXPECT_EQ(tail->OffsetBytes, 8u);
    EXPECT_EQ(tail->SizeBytes, 8u);

    alignment.SizeAlignmentBytes = 16u;
    EXPECT_EQ(RHI::ExpandBufferRangeToAlignment(Buffer(16u),
                                                RHI::BufferRange{.OffsetBytes = 14u, .SizeBytes = 1u},
                                                alignment).error(),
              Core::ErrorCode::OutOfRange);
}

TEST(BufferTransferPlan, SortsCoalescesAndPacksSourceOffsets)
{
    const std::array dirty{
        RHI::BufferRange{.OffsetBytes = 32u, .SizeBytes = 8u},
        RHI::BufferRange{.OffsetBytes = 0u, .SizeBytes = 16u},
        RHI::BufferRange{.OffsetBytes = 12u, .SizeBytes = 8u},
    };

    RHI::BufferPartialWritePlanOptions options;
    options.Alignment.SourceOffsetAlignmentBytes = 16u;
    options.AlignDestinationRanges = false;

    const auto plan = RHI::PlanPartialBufferWrites(Buffer(64u), dirty, options);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->Regions.size(), 2u);

    EXPECT_EQ(plan->Regions[0].DestinationOffsetBytes, 0u);
    EXPECT_EQ(plan->Regions[0].SourceOffsetBytes, 0u);
    EXPECT_EQ(plan->Regions[0].SizeBytes, 20u);

    EXPECT_EQ(plan->Regions[1].DestinationOffsetBytes, 32u);
    EXPECT_EQ(plan->Regions[1].SourceOffsetBytes, 32u);
    EXPECT_EQ(plan->Regions[1].SizeBytes, 8u);

    EXPECT_EQ(plan->TotalDestinationBytes, 28u);
    EXPECT_EQ(plan->TotalSourceBytes, 40u);
}

TEST(BufferTransferPlan, CanPreserveSeparateOverlappingRanges)
{
    const std::array dirty{
        RHI::BufferRange{.OffsetBytes = 0u, .SizeBytes = 16u},
        RHI::BufferRange{.OffsetBytes = 12u, .SizeBytes = 8u},
    };

    RHI::BufferPartialWritePlanOptions options;
    options.AlignDestinationRanges = false;
    options.Coalesce = RHI::BufferPartialWriteCoalesceMode::None;

    const auto plan = RHI::PlanPartialBufferWrites(Buffer(64u), dirty, options);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->Regions.size(), 2u);
    EXPECT_EQ(plan->Regions[0].DestinationOffsetBytes, 0u);
    EXPECT_EQ(plan->Regions[0].SizeBytes, 16u);
    EXPECT_EQ(plan->Regions[1].DestinationOffsetBytes, 12u);
    EXPECT_EQ(plan->Regions[1].SizeBytes, 8u);
}

TEST(BufferTransferPlan, AlignsDirtyRangesBeforeCoalescing)
{
    const std::array dirty{
        RHI::BufferRange{.OffsetBytes = 5u, .SizeBytes = 2u},
        RHI::BufferRange{.OffsetBytes = 9u, .SizeBytes = 2u},
    };

    RHI::BufferPartialWritePlanOptions options;
    options.Alignment.DestinationOffsetAlignmentBytes = 4u;
    options.Alignment.SizeAlignmentBytes = 4u;

    const auto plan = RHI::PlanPartialBufferWrites(Buffer(32u), dirty, options);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->Regions.size(), 1u);
    EXPECT_EQ(plan->Regions[0].DestinationOffsetBytes, 4u);
    EXPECT_EQ(plan->Regions[0].SizeBytes, 8u);
    EXPECT_EQ(plan->TotalSourceBytes, 8u);
}

TEST(BufferTransferPlan, RejectsOutOfRangeDirtyRanges)
{
    const std::array dirty{
        RHI::BufferRange{.OffsetBytes = 60u, .SizeBytes = 8u},
    };

    EXPECT_EQ(RHI::PlanPartialBufferWrites(Buffer(64u), dirty).error(),
              Core::ErrorCode::OutOfRange);
}

TEST(BufferTransferDimensions, ExactTightRangeMatches)
{
    const auto match = RHI::ValidateBufferDimensions(RHI::BufferDimensionMatchDesc{
        .ElementCount = 4u,
        .ComponentBytes = 4u,
        .Region = RHI::BufferRange{.OffsetBytes = 0u, .SizeBytes = 16u},
    });

    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->TightBytes, 16u);
    EXPECT_EQ(match->FootprintBytes, 16u);
    EXPECT_EQ(match->EffectiveStrideBytes, 4u);
    EXPECT_TRUE(match->MatchesExactly);
}

TEST(BufferTransferDimensions, FitWithinAllowsPaddingButExactRejectsIt)
{
    const auto exact = RHI::ValidateBufferDimensions(RHI::BufferDimensionMatchDesc{
        .ElementCount = 3u,
        .ComponentBytes = 4u,
        .Region = RHI::BufferRange{.OffsetBytes = 32u, .SizeBytes = 16u},
    });
    ASSERT_FALSE(exact.has_value());
    EXPECT_EQ(exact.error(), Core::ErrorCode::TypeMismatch);

    const auto fit = RHI::ValidateBufferDimensions(RHI::BufferDimensionMatchDesc{
        .ElementCount = 3u,
        .ComponentBytes = 4u,
        .Region = RHI::BufferRange{.OffsetBytes = 32u, .SizeBytes = 16u},
        .Mode = RHI::BufferDimensionMatchMode::FitWithin,
    });
    ASSERT_TRUE(fit.has_value());
    EXPECT_EQ(fit->FootprintBytes, 12u);
    EXPECT_FALSE(fit->MatchesExactly);
}

TEST(BufferTransferDimensions, StrideDefinesFootprint)
{
    const auto match = RHI::ValidateBufferDimensions(RHI::BufferDimensionMatchDesc{
        .ElementCount = 3u,
        .ComponentBytes = 4u,
        .Region = RHI::BufferRange{.OffsetBytes = 0u, .SizeBytes = 20u},
        .StrideBytes = 8u,
    });

    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->TightBytes, 12u);
    EXPECT_EQ(match->FootprintBytes, 20u);
    EXPECT_EQ(match->EffectiveStrideBytes, 8u);
    EXPECT_TRUE(match->MatchesExactly);

    const auto wrong = RHI::ValidateBufferDimensions(RHI::BufferDimensionMatchDesc{
        .ElementCount = 3u,
        .ComponentBytes = 4u,
        .Region = RHI::BufferRange{.OffsetBytes = 0u, .SizeBytes = 12u},
        .StrideBytes = 8u,
    });
    ASSERT_FALSE(wrong.has_value());
    EXPECT_EQ(wrong.error(), Core::ErrorCode::TypeMismatch);
}

TEST(BufferTransferDimensions, RejectsInvalidStrideAndOverflow)
{
    EXPECT_EQ(RHI::ValidateBufferDimensions(RHI::BufferDimensionMatchDesc{
                  .ElementCount = 1u,
                  .ComponentBytes = 0u,
                  .Region = RHI::BufferRange{.OffsetBytes = 0u, .SizeBytes = 0u},
              }).error(),
              Core::ErrorCode::InvalidArgument);

    EXPECT_EQ(RHI::ValidateBufferDimensions(RHI::BufferDimensionMatchDesc{
                  .ElementCount = 2u,
                  .ComponentBytes = 8u,
                  .Region = RHI::BufferRange{.OffsetBytes = 0u, .SizeBytes = 16u},
                  .StrideBytes = 4u,
              }).error(),
              Core::ErrorCode::InvalidArgument);

    EXPECT_EQ(RHI::ValidateBufferDimensions(RHI::BufferDimensionMatchDesc{
                  .ElementCount = std::numeric_limits<std::uint64_t>::max(),
                  .ComponentBytes = 8u,
                  .Region = RHI::BufferRange{.OffsetBytes = 0u, .SizeBytes = 16u},
                  .StrideBytes = 16u,
              }).error(),
              Core::ErrorCode::OutOfRange);
}
