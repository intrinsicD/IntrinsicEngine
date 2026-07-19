#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>

import Extrinsic.RHI.Profiler;

namespace
{
    namespace RHI = Extrinsic::RHI;
}

TEST(RHIProfiler, QueryRangeMapsAbsoluteIndicesToSlotLocalIndices)
{
    const RHI::TimestampQueryRange range{
        .Base = 32u,
        .Count = 4u,
    };

    EXPECT_FALSE(range.Contains(31u));
    EXPECT_TRUE(range.Contains(32u));
    EXPECT_TRUE(range.Contains(35u));
    EXPECT_FALSE(range.Contains(36u));

    const auto first = range.LocalIndex(32u);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, 0u);

    const auto last = range.LocalIndex(35u);
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ(*last, 3u);

    const auto below = range.LocalIndex(2u);
    ASSERT_FALSE(below.has_value());
    EXPECT_EQ(below.error(), RHI::ProfilerError::InvalidArgument);

    const RHI::TimestampQueryRange nearLimit{
        .Base = std::numeric_limits<std::uint32_t>::max() - 1u,
        .Count = 4u,
    };
    EXPECT_TRUE(nearLimit.Contains(
        std::numeric_limits<std::uint32_t>::max()));
    EXPECT_FALSE(nearLimit.Contains(0u));
}

TEST(RHIProfiler, TimestampDeltaRejectsUnsupportedWidthsAndHandlesWrap)
{
    const auto unsupported = RHI::ComputeTimestampDeltaTicks(1u, 2u, 0u);
    ASSERT_FALSE(unsupported.has_value());
    EXPECT_EQ(unsupported.error(), RHI::ProfilerError::Unsupported);

    const auto invalid = RHI::ComputeTimestampDeltaTicks(1u, 2u, 65u);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error(), RHI::ProfilerError::InvalidArgument);

    const auto wrap32 =
        RHI::ComputeTimestampDeltaTicks(0xfffffff0u, 0x20u, 32u);
    ASSERT_TRUE(wrap32.has_value());
    EXPECT_EQ(*wrap32, 48u);

    const auto wrap64 = RHI::ComputeTimestampDeltaTicks(
        std::numeric_limits<std::uint64_t>::max() - 2u,
        1u,
        64u);
    ASSERT_TRUE(wrap64.has_value());
    EXPECT_EQ(*wrap64, 4u);
}

TEST(RHIProfiler, DurationResolutionRequiresBothAvailableValues)
{
    const auto missingBegin = RHI::ResolveTimestampDurationNs(
        RHI::TimestampQueryValue{.Ticks = 10u, .Available = false},
        RHI::TimestampQueryValue{.Ticks = 20u, .Available = true},
        64u,
        1.0);
    ASSERT_FALSE(missingBegin.has_value());
    EXPECT_EQ(missingBegin.error(), RHI::ProfilerError::NotReady);

    const auto missingEnd = RHI::ResolveTimestampDurationNs(
        RHI::TimestampQueryValue{.Ticks = 10u, .Available = true},
        RHI::TimestampQueryValue{.Ticks = 20u, .Available = false},
        64u,
        1.0);
    ASSERT_FALSE(missingEnd.has_value());
    EXPECT_EQ(missingEnd.error(), RHI::ProfilerError::NotReady);

    const auto available = RHI::ResolveTimestampDurationNs(
        RHI::TimestampQueryValue{.Ticks = 10u, .Available = true},
        RHI::TimestampQueryValue{.Ticks = 20u, .Available = true},
        64u,
        2.0);
    ASSERT_TRUE(available.has_value());
    EXPECT_EQ(*available, 20u);
}

TEST(RHIProfiler, DurationResolutionValidatesPeriodAndCheckedConversion)
{
    constexpr RHI::TimestampQueryValue begin{
        .Ticks = 0u,
        .Available = true,
    };
    constexpr RHI::TimestampQueryValue end{
        .Ticks = 10u,
        .Available = true,
    };

    for (const double invalidPeriod :
         {0.0,
          -1.0,
          std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN()})
    {
        const auto invalid = RHI::ResolveTimestampDurationNs(
            begin,
            end,
            64u,
            invalidPeriod);
        ASSERT_FALSE(invalid.has_value());
        EXPECT_EQ(invalid.error(), RHI::ProfilerError::InvalidArgument);
    }

    const auto overflow = RHI::ResolveTimestampDurationNs(
        begin,
        RHI::TimestampQueryValue{
            .Ticks = std::numeric_limits<std::uint64_t>::max(),
            .Available = true,
        },
        64u,
        2.0);
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error(), RHI::ProfilerError::Overflow);
}

TEST(RHIProfiler, OptionalDurationDistinguishesNativeZeroFromUnavailable)
{
    RHI::GpuTimestampFrame frame{};
    frame.Scopes.push_back(RHI::GpuTimestampScope{
        .Name = "Zero",
        .DurationNs = std::uint64_t{0},
    });
    frame.Scopes.push_back(RHI::GpuTimestampScope{
        .Name = "Unavailable",
        .DurationNs = std::nullopt,
    });

    const std::optional<std::uint64_t> zero =
        frame.FindScopeDurationNs("Zero");
    ASSERT_TRUE(zero.has_value());
    EXPECT_EQ(*zero, 0u);
    EXPECT_FALSE(frame.FindScopeDurationNs("Unavailable").has_value());
    EXPECT_FALSE(frame.FindScopeDurationNs("Missing").has_value());
}
