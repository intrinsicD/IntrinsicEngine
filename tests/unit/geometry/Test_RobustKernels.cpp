#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

import Geometry.Robust;

namespace
{
    using Geometry::Robust::RobustKernel;
    using Geometry::Robust::Rho;
    using Geometry::Robust::Psi;
    using Geometry::Robust::Weight;

    constexpr double kTol = 1e-9;

    const std::vector<RobustKernel> kAllKernels{
        RobustKernel::L2, RobustKernel::L1, RobustKernel::Huber, RobustKernel::Tukey,
        RobustKernel::Welsch, RobustKernel::Lorentzian, RobustKernel::Cauchy};
}

TEST(GeometryRobust, WeightMatchesAnalyticForms)
{
    const double s = 2.0;

    // L2: weight is identically 1.
    EXPECT_NEAR(*Weight(RobustKernel::L2, 5.0, s), 1.0, kTol);

    // Huber: 1 inside the band, c/|u| outside (c = 1.345).
    EXPECT_NEAR(*Weight(RobustKernel::Huber, 1.0, s), 1.0, kTol); // u=0.5 < c
    {
        const double u = 10.0 / s; // 5.0 > c
        EXPECT_NEAR(*Weight(RobustKernel::Huber, 10.0, s), 1.345 / std::abs(u), kTol);
    }

    // Cauchy: 1/(1 + (u/c)^2), c = 2.385.
    {
        const double u = 3.0 / s;
        const double c = 2.385;
        EXPECT_NEAR(*Weight(RobustKernel::Cauchy, 3.0, s), 1.0 / (1.0 + (u * u) / (c * c)), kTol);
    }

    // Tukey: zero weight beyond the cutoff c = 4.685.
    EXPECT_NEAR(*Weight(RobustKernel::Tukey, 100.0, s), 0.0, kTol);
}

TEST(GeometryRobust, WeightEqualsPsiOverU)
{
    const double s = 1.5;
    for (RobustKernel k : kAllKernels)
    {
        for (double r : {0.3, 1.0, 2.5, 6.0})
        {
            const double u = r / s;
            auto psi = Psi(k, r, s);
            auto w = Weight(k, r, s);
            ASSERT_TRUE(psi.has_value());
            ASSERT_TRUE(w.has_value());
            // w(u) = psi(u)/u (away from zero).
            EXPECT_NEAR(*w, *psi / u, 1e-7) << "kernel index " << static_cast<int>(k) << " r=" << r;
        }
    }
}

TEST(GeometryRobust, WeightFiniteAtZeroAndNonIncreasing)
{
    const double s = 1.0;
    for (RobustKernel k : kAllKernels)
    {
        auto w0 = Weight(k, 0.0, s);
        ASSERT_TRUE(w0.has_value());
        EXPECT_TRUE(std::isfinite(*w0)) << "kernel " << static_cast<int>(k);

        // Weight is non-increasing in |r|: larger residuals are downweighted.
        double prev = std::numeric_limits<double>::infinity();
        for (double r : {0.0, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0})
        {
            auto w = Weight(k, r, s);
            ASSERT_TRUE(w.has_value());
            EXPECT_LE(*w, prev + 1e-12) << "kernel " << static_cast<int>(k) << " not monotone at r=" << r;
            prev = *w;
        }
    }
}

TEST(GeometryRobust, RedescendingDownweightsLargeResiduals)
{
    const double s = 1.0;
    // Redescending kernels send weight toward 0 for gross outliers.
    EXPECT_LT(*Weight(RobustKernel::Tukey, 20.0, s), 1e-6);
    EXPECT_LT(*Weight(RobustKernel::Welsch, 20.0, s), 1e-3);
    EXPECT_LT(*Weight(RobustKernel::Cauchy, 50.0, s), 1e-2);
    // Small residuals keep near-unit weight.
    EXPECT_GT(*Weight(RobustKernel::Tukey, 0.1, s), 0.99);
    EXPECT_GT(*Weight(RobustKernel::Welsch, 0.1, s), 0.99);
}

TEST(GeometryRobust, FailClosedOnBadInputs)
{
    for (RobustKernel k : kAllKernels)
    {
        EXPECT_FALSE(Weight(k, 1.0, 0.0).has_value());        // scale <= 0
        EXPECT_FALSE(Weight(k, 1.0, -2.0).has_value());       // negative scale
        EXPECT_FALSE(Rho(k, 1.0, 0.0).has_value());
        EXPECT_FALSE(Psi(k, 1.0, 0.0).has_value());
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        EXPECT_FALSE(Weight(k, nan, 1.0).has_value());        // non-finite residual
        EXPECT_FALSE(Weight(k, 1.0, inf).has_value());        // non-finite scale
        EXPECT_FALSE(Rho(k, inf, 1.0).has_value());
    }
}

TEST(GeometryRobust, RhoIsZeroAtZeroAndPositiveElsewhere)
{
    const double s = 1.0;
    for (RobustKernel k : kAllKernels)
    {
        EXPECT_NEAR(*Rho(k, 0.0, s), 0.0, kTol) << "kernel " << static_cast<int>(k);
        EXPECT_GT(*Rho(k, 2.0, s), 0.0) << "kernel " << static_cast<int>(k);
    }
}
