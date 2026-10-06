#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

import Geometry.Statistics;

namespace
{
    using namespace Geometry::Statistics;

    constexpr double kTol = 1e-9;
}

TEST(GeometryStatistics, StreamingMomentsMatchBatch)
{
    const std::vector<double> xs{2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0, -3.0, 11.5};
    StreamingMoments acc;
    for (double x : xs) acc.Add(x);

    EXPECT_EQ(acc.Count(), xs.size());
    ASSERT_TRUE(acc.Mean().has_value());
    EXPECT_NEAR(*acc.Mean(), 4.85, kTol);
}

TEST(GeometryStatistics, MergeEqualsConcatenation)
{
    const std::vector<double> a{1.0, 2.0, 3.0, 10.0, -4.0};
    const std::vector<double> b{7.0, 7.5, -2.0, 0.0, 100.0, 42.0};
    const std::vector<double> c{-9.0, 6.25, 8.75};

    StreamingMoments accA, accB, accC, accAll;
    for (double x : a) accA.Add(x);
    for (double x : b) accB.Add(x);
    for (double x : c) accC.Add(x);
    for (double x : a) accAll.Add(x);
    for (double x : b) accAll.Add(x);
    for (double x : c) accAll.Add(x);

    const StreamingMoments merged = (accA + accB) + accC;
    EXPECT_EQ(merged.Count(), accAll.Count());
    EXPECT_NEAR(*merged.Mean(), *accAll.Mean(), 1e-7);

    // Commutativity of merge.
    const StreamingMoments mergedCBA = (accC + accB) + accA;
    EXPECT_NEAR(*mergedCBA.Mean(), *merged.Mean(), 1e-7);

    // Associativity within floating-point tolerance.
    const StreamingMoments leftAssoc = (accA + accB) + accC;
    const StreamingMoments rightAssoc = accA + (accB + accC);
    EXPECT_NEAR(*leftAssoc.Mean(), *rightAssoc.Mean(), 1e-7);
}

TEST(GeometryStatistics, MedianOddAndEven)
{
    const std::vector<double> odd{5.0, 1.0, 3.0, 2.0, 4.0};
    auto mOdd = Median(std::span<const double>(odd));
    ASSERT_TRUE(mOdd.has_value());
    EXPECT_NEAR(*mOdd, 3.0, kTol);

    const std::vector<double> even{4.0, 1.0, 3.0, 2.0};
    auto mEven = Median(std::span<const double>(even));
    ASSERT_TRUE(mEven.has_value());
    EXPECT_NEAR(*mEven, 2.5, kTol);
}

TEST(GeometryStatistics, QuantileLinearInterpolation)
{
    // Sorted: 1..5 → numpy-linear quantiles.
    const std::vector<double> xs{5.0, 3.0, 1.0, 4.0, 2.0};
    const std::span<const double> s(xs);
    EXPECT_NEAR(*Quantile(s, 0.0), 1.0, kTol);
    EXPECT_NEAR(*Quantile(s, 0.25), 2.0, kTol);
    EXPECT_NEAR(*Quantile(s, 0.5), 3.0, kTol);
    EXPECT_NEAR(*Quantile(s, 0.75), 4.0, kTol);
    EXPECT_NEAR(*Quantile(s, 1.0), 5.0, kTol);
    // Interpolated point: h = 0.1*(5-1)=0.4 → 1 + 0.4*(2-1) = 1.4
    EXPECT_NEAR(*Quantile(s, 0.1), 1.4, kTol);
}

TEST(GeometryStatistics, GenericMedianAndQuantileSupportNonDoubleVectors)
{
    const std::vector<int> ints{9, 1, 5, 3};
    ASSERT_TRUE(Median(ints).has_value());
    EXPECT_NEAR(*Median(ints), 4.0, kTol);
    ASSERT_TRUE(Quantile(ints, 0.25).has_value());
    EXPECT_NEAR(*Quantile(ints, 0.25), 2.5, kTol);

    const std::vector<float> floats{4.0f, 2.0f, std::numeric_limits<float>::infinity(), 6.0f};
    ASSERT_TRUE(Median(floats).has_value());
    EXPECT_NEAR(*Median(floats), 4.0, kTol);
    ASSERT_TRUE(Quantile(std::span<const float>(floats), 1.0).has_value());
    EXPECT_NEAR(*Quantile(std::span<const float>(floats), 1.0), 6.0, kTol);
}

TEST(GeometryStatistics, RunningMedianTracksMedian)
{
    RunningMedian rm;
    EXPECT_FALSE(rm.Median().has_value());
    const std::vector<double> xs{6.0, 1.0, 4.0, 2.0, 5.0, 3.0};
    std::vector<double> seen;
    for (double x : xs)
    {
        rm.Add(x);
        seen.push_back(x);
        auto ref = Median(std::span<const double>(seen));
        ASSERT_TRUE(rm.Median().has_value());
        ASSERT_TRUE(ref.has_value());
        EXPECT_NEAR(*rm.Median(), *ref, kTol);
    }
}

TEST(GeometryStatistics, SafeTrigClampsOutOfDomain)
{
    EXPECT_NEAR(SafeAcos(1.0 + 1e-9), 0.0, 1e-6);
    EXPECT_NEAR(SafeAcos(-1.0 - 1e-9), std::numbers::pi, 1e-6);
    // In-domain values pass through.
    EXPECT_NEAR(SafeAcos(0.0), std::numbers::pi / 2.0, kTol);
    // Non-finite input fails closed to a defined finite value.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(std::isfinite(SafeAcos(nan)));
}

TEST(GeometryStatistics, FailClosedOnDegenerateInput)
{
    StreamingMoments empty;
    EXPECT_FALSE(empty.Mean().has_value());

    // Non-finite samples are ignored, never poison state.
    StreamingMoments acc;
    acc.Add(std::numeric_limits<double>::infinity());
    acc.Add(std::numeric_limits<double>::quiet_NaN());
    EXPECT_EQ(acc.Count(), 0u);
    acc.Add(3.0);
    acc.Add(5.0);
    EXPECT_EQ(acc.Count(), 2u);
    EXPECT_NEAR(*acc.Mean(), 4.0, kTol);

    const std::vector<double> none{};
    EXPECT_FALSE(Median(std::span<const double>(none)).has_value());
    EXPECT_FALSE(Quantile(std::span<const double>(none), 0.5).has_value());

    const std::vector<double> some{1.0, 2.0, 3.0};
    EXPECT_FALSE(Quantile(std::span<const double>(some), -0.01).has_value());
    EXPECT_FALSE(Quantile(std::span<const double>(some), 1.01).has_value());
    EXPECT_FALSE(Quantile(std::span<const double>(some),
                          std::numeric_limits<double>::quiet_NaN()).has_value());
}
