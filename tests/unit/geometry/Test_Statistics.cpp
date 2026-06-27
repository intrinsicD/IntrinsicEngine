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

    // Batch reference moments over a finite sample set.
    struct BatchMoments
    {
        double mean{};
        double popVar{};
        double skew{};
        double exKurt{};
    };

    BatchMoments ReferenceMoments(const std::vector<double>& xs)
    {
        BatchMoments r{};
        const double n = static_cast<double>(xs.size());
        for (double x : xs) r.mean += x;
        r.mean /= n;
        double m2 = 0.0, m3 = 0.0, m4 = 0.0;
        for (double x : xs)
        {
            const double d = x - r.mean;
            m2 += d * d;
            m3 += d * d * d;
            m4 += d * d * d * d;
        }
        r.popVar = m2 / n;
        r.skew = (std::sqrt(n) * m3) / std::pow(m2, 1.5);
        r.exKurt = (n * m4) / (m2 * m2) - 3.0;
        return r;
    }

    constexpr double kTol = 1e-9;
}

TEST(GeometryStatistics, StreamingMomentsMatchBatch)
{
    const std::vector<double> xs{2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0, -3.0, 11.5};
    StreamingMoments acc;
    for (double x : xs) acc.Add(x);

    const BatchMoments ref = ReferenceMoments(xs);
    ASSERT_TRUE(acc.Mean().has_value());
    EXPECT_NEAR(*acc.Mean(), ref.mean, kTol);
    ASSERT_TRUE(acc.PopulationVariance().has_value());
    EXPECT_NEAR(*acc.PopulationVariance(), ref.popVar, 1e-7);
    ASSERT_TRUE(acc.Skewness().has_value());
    EXPECT_NEAR(*acc.Skewness(), ref.skew, 1e-7);
    ASSERT_TRUE(acc.Kurtosis().has_value());
    EXPECT_NEAR(*acc.Kurtosis(), ref.exKurt, 1e-7);

    // Sample variance = popVar * n / (n-1).
    const double n = static_cast<double>(xs.size());
    ASSERT_TRUE(acc.SampleVariance().has_value());
    EXPECT_NEAR(*acc.SampleVariance(), ref.popVar * n / (n - 1.0), 1e-7);
}

TEST(GeometryStatistics, MergeEqualsConcatenation)
{
    const std::vector<double> a{1.0, 2.0, 3.0, 10.0, -4.0};
    const std::vector<double> b{7.0, 7.5, -2.0, 0.0, 100.0, 42.0};

    StreamingMoments accA, accB, accAll;
    for (double x : a) accA.Add(x);
    for (double x : b) accB.Add(x);
    for (double x : a) accAll.Add(x);
    for (double x : b) accAll.Add(x);

    const StreamingMoments merged = accA + accB;
    EXPECT_EQ(merged.Count(), accAll.Count());
    EXPECT_NEAR(*merged.Mean(), *accAll.Mean(), 1e-7);
    EXPECT_NEAR(*merged.PopulationVariance(), *accAll.PopulationVariance(), 1e-6);
    EXPECT_NEAR(*merged.Skewness(), *accAll.Skewness(), 1e-6);
    EXPECT_NEAR(*merged.Kurtosis(), *accAll.Kurtosis(), 1e-6);

    // Commutativity of merge.
    const StreamingMoments mergedBA = accB + accA;
    EXPECT_NEAR(*mergedBA.Mean(), *merged.Mean(), 1e-7);
    EXPECT_NEAR(*mergedBA.Kurtosis(), *merged.Kurtosis(), 1e-6);
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
    EXPECT_NEAR(SafeAsin(1.0 + 1e-9), std::numbers::pi / 2.0, 1e-6);
    EXPECT_NEAR(SafeAsin(-1.0 - 1e-9), -std::numbers::pi / 2.0, 1e-6);
    // In-domain values pass through.
    EXPECT_NEAR(SafeAcos(0.0), std::numbers::pi / 2.0, kTol);
    // Non-finite input fails closed to a defined finite value.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(std::isfinite(SafeAcos(nan)));
    EXPECT_TRUE(std::isfinite(SafeAsin(nan)));
}

TEST(GeometryStatistics, FailClosedOnDegenerateInput)
{
    StreamingMoments empty;
    EXPECT_FALSE(empty.Mean().has_value());
    EXPECT_FALSE(empty.PopulationVariance().has_value());
    EXPECT_FALSE(empty.Skewness().has_value());
    EXPECT_FALSE(empty.Kurtosis().has_value());

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
