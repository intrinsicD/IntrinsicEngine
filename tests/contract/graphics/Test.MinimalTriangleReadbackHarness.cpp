#include <gtest/gtest.h>

#include "MinimalTriangleReadback.hpp"
#include "OperationalCounterStability.hpp"

namespace Readback = Extrinsic::Tests::Support::MinimalTriangleReadback;
namespace Counters = Extrinsic::Tests::Support::OperationalCounterStability;

TEST(MinimalTriangleReadbackHarness, SamplePointsHaveFourEntriesWithMixedMembership)
{
    ASSERT_EQ(Readback::kSamplePoints.size(), 4u);

    std::size_t insideCount = 0;
    std::size_t outsideCount = 0;
    for (const auto& point : Readback::kSamplePoints)
    {
        EXPECT_LT(point.PixelX, Readback::kFramebufferWidth);
        EXPECT_LT(point.PixelY, Readback::kFramebufferHeight);
        if (point.InsideTriangle)
        {
            ++insideCount;
        }
        else
        {
            ++outsideCount;
        }
    }

    EXPECT_GE(insideCount, 1u);
    EXPECT_GE(outsideCount, 1u);
    EXPECT_EQ(insideCount + outsideCount, Readback::kSamplePoints.size());
}

TEST(MinimalTriangleReadbackHarness, ExpectedColorsFollowMembership)
{
    for (const auto& point : Readback::kSamplePoints)
    {
        const auto expected = Readback::ExpectedAt(point);
        if (point.InsideTriangle)
        {
            EXPECT_EQ(expected.R, Readback::Quantize8(Readback::kTriangleR)) << point.Label;
            EXPECT_EQ(expected.G, Readback::Quantize8(Readback::kTriangleG)) << point.Label;
            EXPECT_EQ(expected.B, Readback::Quantize8(Readback::kTriangleB)) << point.Label;
            EXPECT_EQ(expected.A, Readback::Quantize8(Readback::kTriangleA)) << point.Label;
        }
        else
        {
            EXPECT_EQ(expected.R, Readback::Quantize8(Readback::kClearR)) << point.Label;
            EXPECT_EQ(expected.G, Readback::Quantize8(Readback::kClearG)) << point.Label;
            EXPECT_EQ(expected.B, Readback::Quantize8(Readback::kClearB)) << point.Label;
            EXPECT_EQ(expected.A, Readback::Quantize8(Readback::kClearA)) << point.Label;
        }
    }
}

TEST(MinimalTriangleReadbackHarness, ToleranceAcceptsExactAndNearMatches)
{
    constexpr Readback::ExpectedPixel reference{140, 51, 217, 255};

    EXPECT_TRUE(Readback::ChannelsWithinTolerance(reference, reference));

    Readback::ExpectedPixel jitter{
        static_cast<std::uint8_t>(reference.R - Readback::kDefaultChannelTolerance),
        static_cast<std::uint8_t>(reference.G + Readback::kDefaultChannelTolerance),
        reference.B,
        reference.A,
    };
    EXPECT_TRUE(Readback::ChannelsWithinTolerance(reference, jitter));

    Readback::ExpectedPixel beyond{
        static_cast<std::uint8_t>(reference.R - Readback::kDefaultChannelTolerance - 1),
        reference.G,
        reference.B,
        reference.A,
    };
    EXPECT_FALSE(Readback::ChannelsWithinTolerance(reference, beyond));
}

TEST(MinimalTriangleReadbackHarness, InteriorCenterSampleIsInsideTriangleNdc)
{
    // The interior center sample is the deterministic membership anchor: it
    // maps to NDC (0, 0), which is inside the (-0.5,-0.5), (0.5,-0.5), (0,0.5)
    // triangle regardless of viewport Y orientation.
    const auto& interior = Readback::kSamplePoints[0];
    EXPECT_EQ(interior.Label, "interior_center");
    EXPECT_EQ(interior.PixelX, Readback::kFramebufferWidth / 2u);
    EXPECT_EQ(interior.PixelY, Readback::kFramebufferHeight / 2u);
    EXPECT_TRUE(interior.InsideTriangle);
}

TEST(OperationalCounterStability, StableSnapshotMatchesSelf)
{
    const Counters::Snapshot snapshot{1u, 2u, 3u, 4u};
    EXPECT_TRUE(Counters::IsStable(snapshot, snapshot));
}

TEST(OperationalCounterStability, AnyCounterIncrementFailsStability)
{
    const Counters::Snapshot before{10u, 20u, 30u, 40u};

    EXPECT_FALSE(Counters::IsStable(before, Counters::Snapshot{11u, 20u, 30u, 40u}));
    EXPECT_FALSE(Counters::IsStable(before, Counters::Snapshot{10u, 21u, 30u, 40u}));
    EXPECT_FALSE(Counters::IsStable(before, Counters::Snapshot{10u, 20u, 31u, 40u}));
    EXPECT_FALSE(Counters::IsStable(before, Counters::Snapshot{10u, 20u, 30u, 41u}));
}
