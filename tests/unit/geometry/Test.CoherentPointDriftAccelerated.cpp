// METHOD-049: optimized Coherent Point Drift backends against the METHOD-015 reference.
// Frozen parity tolerances (normalized unit-cube fixtures):
//   dense parallel:   transform entries and moved points within 1e-9 of the reference
//   truncated (1e-8): transform entries and moved points within 1e-6 of the reference
//   low-rank nonrigid: moved points within 2e-3 of the full solution at k = 60
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

import Geometry.Registration.CoherentPointDrift;

namespace CPD = Geometry::CoherentPointDrift;
namespace
{
    std::vector<glm::vec3> Cloud(std::size_t count, std::uint32_t seed)
    {
        std::mt19937 random(seed);
        std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
        std::vector<glm::vec3> points(count);
        for (auto& p : points) p = {uniform(random), uniform(random), 0.6f * uniform(random)};
        return points;
    }

    std::vector<glm::vec3> Rigid(const std::vector<glm::vec3>& points, double angle, glm::dvec3 t, double noise,
                                 std::uint32_t seed)
    {
        const glm::dmat3 r(glm::rotate(glm::dmat4(1.0), angle, glm::normalize(glm::dvec3(1.0, 2.0, 0.5))));
        std::mt19937 random(seed);
        std::normal_distribution<double> gaussian(0.0, noise);
        std::vector<glm::vec3> out;
        for (const auto& p : points)
            out.push_back(glm::vec3(r * glm::dvec3(p) + t + glm::dvec3(gaussian(random), gaussian(random), gaussian(random))));
        return out;
    }

    std::vector<glm::vec3> Bent(const std::vector<glm::vec3>& points)
    {
        std::vector<glm::vec3> out;
        for (const auto& p : points)
            out.push_back(p + glm::vec3(0.0f, 0.1f * std::sin(2.0f * p.x), 0.08f * std::cos(1.5f * p.y)));
        return out;
    }

    double MaxPointDifference(const CPD::Result& a, const CPD::Result& b)
    {
        double worst = 0.0;
        for (std::size_t i = 0; i < a.TransformedSource.size(); ++i)
            worst = std::max(worst, glm::length(a.TransformedSource[i] - b.TransformedSource[i]));
        return worst;
    }

    double MaxTransformDifference(const CPD::Result& a, const CPD::Result& b)
    {
        double worst = 0.0;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) worst = std::max(worst, std::abs(a.Transform[c][r] - b.Transform[c][r]));
        return worst;
    }

    struct Soa
    {
        std::vector<double> X, Y, Z;
        explicit Soa(const std::vector<glm::vec3>& p)
        {
            for (const auto& q : p) { X.push_back(q.x); Y.push_back(q.y); Z.push_back(q.z); }
        }
        [[nodiscard]] CPD::EStep::PointSet View() const { return {X, Y, Z}; }
    };
}

TEST(CoherentPointDriftAccelerated, DenseParallelMatchesTheReferenceForEveryVariant)
{
    const auto source = Cloud(300, 5);
    const auto target = Rigid(source, 0.4, {0.3, -0.1, 0.2}, 0.004, 9);
    const auto bent = Bent(source);
    for (const CPD::Variant variant : {CPD::Variant::Rigid, CPD::Variant::Affine, CPD::Variant::Nonrigid})
    {
        const auto& fixed = variant == CPD::Variant::Nonrigid ? bent : target;
        const CPD::Params reference{.Method = variant, .OutlierWeight = 0.1, .MaxIterations = 60};
        CPD::Params dense = reference;
        dense.EStep = CPD::EStepPolicy::Dense;
        dense.Threads = 4;
        const auto a = CPD::Register(fixed, source, reference);
        const auto b = CPD::Register(fixed, source, dense);
        ASSERT_TRUE(a.Succeeded() && b.Succeeded()) << CPD::ToString(variant);
        EXPECT_EQ(b.Backend, "cpu_dense_parallel");
        EXPECT_EQ(a.Iterations, b.Iterations) << CPD::ToString(variant);
        EXPECT_LE(MaxTransformDifference(a, b), 1e-9) << CPD::ToString(variant);
        EXPECT_LE(MaxPointDifference(a, b), 1e-9) << CPD::ToString(variant);
        EXPECT_EQ(b.EStepErrorBound, 0.0);
    }
}

TEST(CoherentPointDriftAccelerated, ResultsAreBitwiseIdenticalAcrossThreadCounts)
{
    const auto source = Cloud(500, 21);
    const auto target = Rigid(source, 0.3, {0.1, 0.2, -0.1}, 0.003, 4);
    for (const CPD::EStepPolicy policy : {CPD::EStepPolicy::Dense, CPD::EStepPolicy::Truncated, CPD::EStepPolicy::Auto,
                                          CPD::EStepPolicy::Nystrom})
    {
        std::vector<CPD::Result> runs;
        for (const std::uint32_t threads : {1u, 3u, 8u})
            runs.push_back(CPD::Register(target, source, CPD::Params{.OutlierWeight = 0.05, .MaxIterations = 40,
                                                                     .EStep = policy, .Threads = threads,
                                                                     .NystromLandmarks = 16u}));
        for (const auto& run : runs)
        {
            ASSERT_TRUE(run.Succeeded());
            EXPECT_EQ(run.Iterations, runs[0].Iterations);
            EXPECT_EQ(run.Sigma2, runs[0].Sigma2);
            for (std::size_t i = 0; i < run.TransformedSource.size(); ++i)
                ASSERT_EQ(run.TransformedSource[i], runs[0].TransformedSource[i]) << CPD::ToString(policy);
        }
    }
}

TEST(CoherentPointDriftAccelerated, TruncatedStaysWithinToleranceOfTheReference)
{
    const auto source = Cloud(600, 8);
    const auto target = Rigid(source, 0.5, {0.2, 0.1, 0.0}, 0.003, 2);
    const CPD::Params reference{.OutlierWeight = 0.1, .MaxIterations = 80};
    CPD::Params truncated = reference;
    truncated.EStep = CPD::EStepPolicy::Truncated;
    truncated.EStepTolerance = 1e-8;
    const auto a = CPD::Register(target, source, reference);
    std::uint64_t lastEvaluations = 0u;
    const auto b = CPD::Register(target, source, truncated,
                                 [&](const CPD::IterationTrace& t) { lastEvaluations = t.KernelEvaluations; });
    ASSERT_TRUE(a.Succeeded() && b.Succeeded());
    EXPECT_EQ(b.Backend, "cpu_truncated");
    EXPECT_LE(b.EStepErrorBound, 1e-8);
    EXPECT_LE(MaxTransformDifference(a, b), 1e-6);
    EXPECT_LE(MaxPointDifference(a, b), 1e-6);
    // Once sigma is small, both passes together evaluate a fraction of the N M dense terms.
    EXPECT_LT(lastEvaluations, std::uint64_t(source.size() * target.size()) / 4u);
}

TEST(CoherentPointDriftAccelerated, TruncationBoundCoversTheActualExpectationError)
{
    const auto targetPoints = Cloud(400, 13);
    const auto movedPoints = Rigid(Cloud(350, 17), 0.05, {0.02, 0.0, 0.01}, 0.01, 6);
    const Soa target(targetPoints), moved(movedPoints);
    CPD::EStep::Evaluator dense, truncated;
    dense.SetTarget(target.View());
    truncated.SetTarget(target.View());
    for (const double sigma2 : {1e-4, 1e-3, 1e-2, 0.1})
        for (const double logOutlier : {-std::numeric_limits<double>::infinity(), std::log(0.05)})
            for (const double tolerance : {1e-3, 1e-6})
            {
                CPD::EStep::Sums exact, approx;
                ASSERT_TRUE(dense.Evaluate(moved.View(), sigma2, logOutlier, {.Policy = CPD::EStepPolicy::Dense}, exact));
                ASSERT_TRUE(truncated.Evaluate(moved.View(), sigma2, logOutlier,
                                               {.Policy = CPD::EStepPolicy::Truncated, .Tolerance = tolerance}, approx));
                EXPECT_EQ(approx.Used, CPD::EStepPolicy::Truncated);
                EXPECT_LE(approx.ErrorBound, tolerance);
                const double bound = approx.ErrorBound;
                // Each row denominator has relative error <= bound, hence the log-sum and Pt1.
                EXPECT_LE(std::abs(exact.LogDenominatorSum - approx.LogDenominatorSum),
                          double(targetPoints.size()) * std::log1p(bound) + 1e-9);
                for (std::size_t j = 0; j < exact.Pt1.size(); ++j)
                    ASSERT_LE(std::abs(exact.Pt1[j] - approx.Pt1[j]), 2.0 * bound + 1e-12) << "sigma2=" << sigma2;
                double p1Error = 0.0, p1Total = 0.0;
                for (std::size_t i = 0; i < exact.P1.size(); ++i)
                {
                    p1Error += std::abs(exact.P1[i] - approx.P1[i]);
                    p1Total += exact.P1[i];
                }
                EXPECT_LE(p1Error, 2.0 * bound * p1Total + 1e-12) << "sigma2=" << sigma2;
            }
}

TEST(CoherentPointDriftAccelerated, FastGaussBoundCoversTheActualExpectationError)
{
    const auto targetPoints = Cloud(600, 61);
    const auto movedPoints = Rigid(Cloud(500, 62), 0.2, {0.1, 0.0, 0.05}, 0.01, 8);
    const Soa target(targetPoints), moved(movedPoints);
    CPD::EStep::Evaluator dense, fast;
    dense.SetTarget(target.View());
    fast.SetTarget(target.View());
    std::size_t planned = 0;
    for (const double sigma2 : {0.5, 2.0, 8.0})
        for (const double tolerance : {1e-4, 1e-7})
        {
            CPD::EStep::Sums exact, approx;
            ASSERT_TRUE(dense.Evaluate(moved.View(), sigma2, std::log(0.05), {.Policy = CPD::EStepPolicy::Dense}, exact));
            ASSERT_TRUE(fast.Evaluate(moved.View(), sigma2, std::log(0.05),
                                      {.Policy = CPD::EStepPolicy::FastGauss, .Tolerance = tolerance}, approx));
            if (approx.Used != CPD::EStepPolicy::FastGauss) continue; // no plan met the bound: exact dense
            ++planned;
            const double bound = approx.ErrorBound;
            EXPECT_LE(bound, 2.0 * tolerance) << "sigma2=" << sigma2;
            EXPECT_LE(std::abs(exact.LogDenominatorSum - approx.LogDenominatorSum),
                      double(targetPoints.size()) * std::log1p(bound) + 1e-9);
            for (std::size_t j = 0; j < exact.Pt1.size(); ++j)
                ASSERT_LE(std::abs(exact.Pt1[j] - approx.Pt1[j]), 2.0 * bound + 1e-12) << "sigma2=" << sigma2;
            for (std::size_t i = 0; i < exact.P1.size(); ++i)
                ASSERT_LE(std::abs(exact.P1[i] - approx.P1[i]), 2.0 * bound * exact.P1[i] + 1e-12) << "sigma2=" << sigma2;
        }
    EXPECT_GE(planned, 4u) << "wide kernels should admit fast Gauss plans";
}

TEST(CoherentPointDriftAccelerated, FastGaussRegistrationMatchesTheReference)
{
    const auto source = Cloud(400, 71);
    const auto target = Rigid(source, 0.3, {0.2, 0.1, -0.1}, 0.003, 5);
    const CPD::Params reference{.OutlierWeight = 0.05, .MaxIterations = 60};
    CPD::Params fast = reference;
    fast.EStep = CPD::EStepPolicy::FastGauss;
    fast.EStepTolerance = 1e-9;
    std::size_t fastIterations = 0;
    const auto a = CPD::Register(target, source, reference);
    const auto b = CPD::Register(target, source, fast, [&](const CPD::IterationTrace& t)
                                 { fastIterations += t.EStep == CPD::EStepPolicy::FastGauss ? 1u : 0u; });
    ASSERT_TRUE(a.Succeeded() && b.Succeeded());
    EXPECT_EQ(b.RequestedBackend, "cpu_ifgt");
    // Iterations whose plan misses the bound fall back to dense; the result says so.
    EXPECT_TRUE(b.Backend == "cpu_ifgt" || b.Backend == "cpu_mixed") << b.Backend;
    EXPECT_GT(fastIterations, 0u);
    EXPECT_LE(b.EStepErrorBound, 2e-9);
    EXPECT_LE(MaxPointDifference(a, b), 1e-5);
}

// METHOD-053: the Nystroem E-step's sampled error is an estimate; on wide kernels it tracks the
// actual error of every row, and narrow kernels are rejected and redone exactly.
TEST(CoherentPointDriftAccelerated, NystromSampledErrorTracksTheActualExpectationError)
{
    // Large enough that 48 landmarks cost well below the dense rows (small inputs run exactly).
    const auto targetPoints = Cloud(1200, 81);
    const auto movedPoints = Rigid(Cloud(1000, 82), 0.2, {0.1, 0.0, 0.05}, 0.01, 9);
    const Soa target(targetPoints), moved(movedPoints);
    CPD::EStep::Evaluator dense, nystrom;
    dense.SetTarget(target.View());
    for (const double sigma2 : {8.0, 2.0, 0.5, 0.1})
    {
        nystrom.SetTarget(target.View()); // no rejection memory between the kernels
        CPD::EStep::Sums exact, approx;
        ASSERT_TRUE(dense.Evaluate(moved.View(), sigma2, std::log(0.05), {.Policy = CPD::EStepPolicy::Dense}, exact));
        ASSERT_TRUE(nystrom.Evaluate(moved.View(), sigma2, std::log(0.05),
                                     {.Policy = CPD::EStepPolicy::Nystrom, .NystromLandmarks = 48u,
                                      .NystromErrorLimit = 1.0}, approx));
        ASSERT_EQ(approx.Used, CPD::EStepPolicy::Nystrom) << "sigma2=" << sigma2;
        EXPECT_EQ(approx.ErrorBound, 0.0) << "no bound is claimed";
        double pt1 = 0.0, p1 = 0.0;
        for (std::size_t j = 0; j < exact.Pt1.size(); ++j) pt1 = std::max(pt1, std::abs(exact.Pt1[j] - approx.Pt1[j]));
        for (std::size_t i = 0; i < exact.P1.size(); ++i)
            p1 = std::max(p1, std::abs(exact.P1[i] - approx.P1[i]) / exact.P1[i]);
        std::printf("sigma2 %g: sampled %.3g, max Pt1 delta %.3g, max relative P1 delta %.3g\n", sigma2,
                    approx.SampledError, pt1, p1);
        // Wide kernels are nearly low rank: the approximation is accurate, and the estimate from
        // 64 sampled rows is within a small factor of the worst of all rows (measured: <= 3x).
        if (sigma2 >= 8.0) EXPECT_LE(p1, 1e-6) << "sigma2=" << sigma2;
        EXPECT_LE(p1, 10.0 * approx.SampledError + 1e-12) << "sigma2=" << sigma2;
    }
    // A narrow kernel misses the default limit and is evaluated exactly.
    nystrom.SetTarget(target.View());
    CPD::EStep::Sums exact, fallback;
    ASSERT_TRUE(nystrom.Evaluate(moved.View(), 1e-4, std::log(0.05),
                                 {.Policy = CPD::EStepPolicy::Nystrom, .NystromLandmarks = 48u}, fallback));
    EXPECT_NE(fallback.Used, CPD::EStepPolicy::Nystrom);
    EXPECT_EQ(fallback.SampledError, 0.0);
    ASSERT_TRUE(dense.Evaluate(moved.View(), 1e-4, std::log(0.05), {.Policy = fallback.Used}, exact));
    EXPECT_EQ(exact.LogDenominatorSum, fallback.LogDenominatorSum);
    EXPECT_EQ(exact.P1, fallback.P1);
}

TEST(CoherentPointDriftAccelerated, NystromRunsExactlyWhereItWouldNotPay)
{
    const auto source = Cloud(300, 93);
    const auto target = Rigid(source, 0.2, {0.1, 0.0, 0.0}, 0.003, 3);
    std::size_t nystromIterations = 0;
    const auto result = CPD::Register(target, source, CPD::Params{.OutlierWeight = 0.05, .EStep = CPD::EStepPolicy::Nystrom},
                                      [&](const CPD::IterationTrace& t) { nystromIterations += t.EStep == CPD::EStepPolicy::Nystrom; });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(nystromIterations, 0u) << "256 landmarks cost more than the dense rows of 300 x 300 points";
    EXPECT_EQ(result.EStepSampledError, 0.0);
    EXPECT_EQ(result.Backend, "cpu_auto") << "no iteration was approximated";
}

TEST(CoherentPointDriftAccelerated, NystromRegistrationMatchesTheReference)
{
    const auto source = Cloud(1500, 91);
    const auto target = Rigid(source, 0.3, {0.2, 0.1, -0.1}, 0.003, 5);
    const CPD::Params reference{.OutlierWeight = 0.05, .MaxIterations = 80};
    CPD::Params approximate = reference;
    approximate.EStep = CPD::EStepPolicy::Nystrom;
    approximate.NystromLandmarks = 64u;
    std::size_t nystromIterations = 0, exactIterations = 0;
    const auto a = CPD::Register(target, source, reference);
    const auto b = CPD::Register(target, source, approximate, [&](const CPD::IterationTrace& t)
    {
        (t.EStep == CPD::EStepPolicy::Nystrom ? nystromIterations : exactIterations) += 1u;
        EXPECT_EQ(t.EStepSampledError > 0.0, t.EStep == CPD::EStepPolicy::Nystrom);
    });
    ASSERT_TRUE(a.Succeeded() && b.Succeeded());
    EXPECT_EQ(b.RequestedBackend, "cpu_nystrom");
    EXPECT_EQ(b.Backend, "cpu_nystrom");
    // Wide early iterations are approximated, the narrow tail runs exactly.
    EXPECT_GT(nystromIterations, 0u);
    EXPECT_GT(exactIterations, 0u);
    EXPECT_LE(b.EStepSampledError, approximate.NystromErrorLimit);
    EXPECT_LE(b.KernelEvaluations, a.KernelEvaluations);
    std::printf("nystrom %zu, exact %zu iterations; reference %u; max point delta %.3g\n", nystromIterations,
                exactIterations, a.Iterations, MaxPointDifference(a, b));
    EXPECT_LE(MaxPointDifference(a, b), 1e-5);
}

TEST(CoherentPointDriftAccelerated, WeightedRowsShiftByTheirLargestWeightedTerm)
{
    // A down-weighted nearest source must not underflow the row: log-weights [-800, 0] with the
    // far source at squared distance 1600 (sigma^2 = 1) give equal terms e^{-800}.
    const Soa target(std::vector<glm::vec3>{{0.0f, 0.0f, 0.0f}});
    const Soa moved(std::vector<glm::vec3>{{0.0f, 0.0f, 0.0f}, {40.0f, 0.0f, 0.0f}});
    const std::vector<double> logWeights{-800.0, 0.0};
    CPD::EStep::Evaluator evaluator;
    evaluator.SetTarget(target.View());
    for (const CPD::EStepPolicy policy : {CPD::EStepPolicy::Dense, CPD::EStepPolicy::Truncated})
        for (const std::size_t budget : {std::size_t{128} << 20, std::size_t{0}})
        {
            CPD::EStep::Sums sums;
            ASSERT_TRUE(evaluator.Evaluate(moved.View(), 1.0, -std::numeric_limits<double>::infinity(),
                                           {.Policy = policy, .Tolerance = 1e-6, .PartialBudgetBytes = budget}, sums,
                                           logWeights))
                << CPD::ToString(policy);
            EXPECT_NEAR(sums.P1[0], 0.5, 1e-12) << CPD::ToString(policy);
            EXPECT_NEAR(sums.P1[1], 0.5, 1e-12) << CPD::ToString(policy);
            EXPECT_NEAR(sums.LogDenominatorSum, std::log(2.0) - 800.0, 1e-9) << CPD::ToString(policy);
        }
}

TEST(CoherentPointDriftAccelerated, TwoPassFallbackMatchesTheBlockedSinglePass)
{
    const Soa target(Cloud(300, 51)), moved(Rigid(Cloud(280, 52), 0.1, {0.05, 0.0, 0.0}, 0.01, 3));
    CPD::EStep::Evaluator evaluator;
    evaluator.SetTarget(target.View());
    for (const CPD::EStepPolicy policy : {CPD::EStepPolicy::Dense, CPD::EStepPolicy::Truncated})
        for (const double sigma2 : {1e-3, 0.05})
        {
            CPD::EStep::Sums blocked, twoPass;
            ASSERT_TRUE(evaluator.Evaluate(moved.View(), sigma2, std::log(0.02), {.Policy = policy, .Tolerance = 1e-7}, blocked));
            ASSERT_TRUE(evaluator.Evaluate(moved.View(), sigma2, std::log(0.02),
                                           {.Policy = policy, .Tolerance = 1e-7, .PartialBudgetBytes = 0u}, twoPass));
            EXPECT_EQ(twoPass.KernelEvaluations, 2u * blocked.KernelEvaluations) << "two passes evaluate every term twice";
            EXPECT_NEAR(blocked.LogDenominatorSum, twoPass.LogDenominatorSum, 1e-9);
            for (std::size_t i = 0; i < blocked.P1.size(); ++i)
            {
                ASSERT_NEAR(blocked.P1[i], twoPass.P1[i], 1e-12 * (1.0 + blocked.P1[i]));
                ASSERT_NEAR(blocked.PXx[i], twoPass.PXx[i], 1e-12 * (1.0 + std::abs(blocked.PXx[i])));
            }
            for (std::size_t j = 0; j < blocked.Pt1.size(); ++j) ASSERT_NEAR(blocked.Pt1[j], twoPass.Pt1[j], 1e-12);
        }
}

TEST(CoherentPointDriftAccelerated, AutoUsesDenseForWideAndTruncatedForNarrowKernels)
{
    const auto source = Cloud(500, 31);
    const auto target = Rigid(source, 0.6, {0.4, -0.3, 0.2}, 0.002, 7);
    std::vector<CPD::IterationTrace> trace;
    const auto result = CPD::Register(target, source,
                                      CPD::Params{.OutlierWeight = 0.05, .MaxIterations = 100, .EStep = CPD::EStepPolicy::Auto},
                                      [&](const CPD::IterationTrace& t) { trace.push_back(t); });
    ASSERT_TRUE(result.Succeeded());
    ASSERT_GE(trace.size(), 5u);
    EXPECT_EQ(result.Backend, "cpu_auto");
    EXPECT_EQ(trace.front().EStep, CPD::EStepPolicy::Dense);
    EXPECT_EQ(trace.back().EStep, CPD::EStepPolicy::Truncated);
    const auto reference = CPD::Register(target, source, CPD::Params{.OutlierWeight = 0.05, .MaxIterations = 100});
    EXPECT_LE(MaxTransformDifference(reference, result), 1e-4);
}

namespace
{
    // The ExternalRequest contract in plain doubles: what a device evaluator must compute.
    bool ReferenceExternal(const CPD::EStep::ExternalRequest& r, std::size_t& calls)
    {
        ++calls;
        const std::size_t n = r.Target.Size(), m = r.Moved.Size();
        const auto a = [&](std::size_t j, std::size_t i) {
            const double dx = r.Target.X[j] - r.Moved.X[i], dy = r.Target.Y[j] - r.Moved.Y[i], dz = r.Target.Z[j] - r.Moved.Z[i];
            return r.LogWeights[i] - (dx * dx + dy * dy + dz * dz) / (2.0 * r.Sigma2);
        };
        for (std::size_t j = 0; j < n; ++j)
        {
            double top = -std::numeric_limits<double>::infinity(), sum = 0.0;
            for (std::size_t i = 0; i < m; ++i) top = std::max(top, a(j, i));
            for (std::size_t i = 0; i < m; ++i) sum += std::exp(a(j, i) - top);
            r.LogDenominator[j] = top + std::log(sum + std::exp(r.LogOutlier - top));
            r.Pt1[j] = sum * std::exp(top - r.LogDenominator[j]);
        }
        for (std::size_t i = 0; i < m; ++i)
        {
            double p1 = 0.0, px = 0.0, py = 0.0, pz = 0.0;
            for (std::size_t j = 0; j < n; ++j)
            {
                const double p = std::exp(a(j, i) - r.LogDenominator[j]);
                p1 += p; px += p * r.Target.X[j]; py += p * r.Target.Y[j]; pz += p * r.Target.Z[j];
            }
            r.P1[i] = p1; r.PXx[i] = px; r.PXy[i] = py; r.PXz[i] = pz;
        }
        return true;
    }
}

TEST(CoherentPointDriftAccelerated, VulkanPolicyRunsWideKernelsOnTheExternalEvaluator)
{
    // METHOD-056 seam: wide kernels go to the evaluator (here the contract in doubles, so the
    // statistics match dense), narrow ones to the CPU truncated path; weighted rows and the
    // outlier term travel in the shifted frame.
    const Soa target(Cloud(240, 61)), moved(Rigid(Cloud(220, 62), 0.2, {0.1, 0.0, 0.05}, 0.01, 4));
    std::vector<double> logWeights(220);
    for (std::size_t i = 0; i < logWeights.size(); ++i) logWeights[i] = -3.0 + 0.01 * double(i);
    CPD::EStep::Evaluator evaluator;
    evaluator.SetTarget(target.View());
    std::size_t calls = 0;
    const CPD::EStep::Settings vulkan{.Policy = CPD::EStepPolicy::Vulkan, .Tolerance = 1e-6,
        .External = [&calls](const CPD::EStep::ExternalRequest& r) { return ReferenceExternal(r, calls); }};
    for (const bool weighted : {false, true})
    {
        const std::span<const double> weights = weighted ? std::span<const double>(logWeights) : std::span<const double>{};
        CPD::EStep::Sums device, dense;
        ASSERT_TRUE(evaluator.Evaluate(moved.View(), 0.5, std::log(0.02), vulkan, device, weights));
        ASSERT_TRUE(evaluator.Evaluate(moved.View(), 0.5, std::log(0.02), {.Policy = CPD::EStepPolicy::Dense}, dense, weights));
        EXPECT_EQ(device.Used, CPD::EStepPolicy::Vulkan);
        EXPECT_FALSE(device.ExternalFallback);
        EXPECT_EQ(device.KernelEvaluations, 2u * 240u * 220u);
        EXPECT_NEAR(device.LogDenominatorSum, dense.LogDenominatorSum, 1e-9 * std::abs(dense.LogDenominatorSum));
        EXPECT_NEAR(device.Matched, dense.Matched, 1e-10 * dense.Matched);
        for (std::size_t i = 0; i < dense.P1.size(); ++i)
        {
            ASSERT_NEAR(device.P1[i], dense.P1[i], 1e-12 * (1.0 + dense.P1[i]));
            ASSERT_NEAR(device.PXy[i], dense.PXy[i], 1e-12 * (1.0 + std::abs(dense.PXy[i])));
        }
    }
    EXPECT_EQ(calls, 2u);
    CPD::EStep::Sums narrow;
    ASSERT_TRUE(evaluator.Evaluate(moved.View(), 1e-4, std::log(0.02), vulkan, narrow));
    EXPECT_EQ(narrow.Used, CPD::EStepPolicy::Truncated);
    EXPECT_FALSE(narrow.ExternalFallback);
    EXPECT_EQ(calls, 2u) << "narrow kernels stay on the CPU";
}

TEST(CoherentPointDriftAccelerated, VulkanPolicyFallsBackToTheCpuAndSaysSo)
{
    const Soa target(Cloud(200, 63)), moved(Cloud(190, 64));
    CPD::EStep::Evaluator evaluator;
    evaluator.SetTarget(target.View());
    CPD::EStep::Sums dense;
    ASSERT_TRUE(evaluator.Evaluate(moved.View(), 0.5, std::log(0.02), {.Policy = CPD::EStepPolicy::Dense}, dense));
    const auto failing = [](const CPD::EStep::ExternalRequest&) { return false; };
    const auto poisoned = [](const CPD::EStep::ExternalRequest& r) {
        std::size_t calls = 0;
        ReferenceExternal(r, calls);
        r.P1[3] = std::numeric_limits<double>::quiet_NaN();
        return true;
    };
    for (const CPD::EStep::ExternalEvaluator& external :
         {CPD::EStep::ExternalEvaluator{}, CPD::EStep::ExternalEvaluator{failing}, CPD::EStep::ExternalEvaluator{poisoned}})
    {
        CPD::EStep::Sums sums;
        ASSERT_TRUE(evaluator.Evaluate(moved.View(), 0.5, std::log(0.02),
                                       {.Policy = CPD::EStepPolicy::Vulkan, .External = external}, sums));
        EXPECT_EQ(sums.Used, CPD::EStepPolicy::Dense);
        EXPECT_TRUE(sums.ExternalFallback);
        EXPECT_EQ(sums.LogDenominatorSum, dense.LogDenominatorSum) << "the fallback is the exact dense pass";
        EXPECT_EQ(sums.P1, dense.P1);
    }

    // Solver reporting: the device backend when it ran, otherwise the exact Auto choice with
    // every device-bound iteration counted as a fallback.
    const auto source = Cloud(300, 65);
    const auto registered = Rigid(source, 0.4, {0.2, -0.1, 0.1}, 0.003, 8);
    std::size_t calls = 0;
    CPD::Params params{.OutlierWeight = 0.05, .MaxIterations = 60, .EStep = CPD::EStepPolicy::Vulkan};
    const auto cpu = CPD::Register(registered, source, params);
    ASSERT_TRUE(cpu.Succeeded());
    EXPECT_EQ(cpu.RequestedBackend, "gpu_vulkan_fp32_dense");
    EXPECT_EQ(cpu.Backend, "cpu_auto");
    EXPECT_GT(cpu.EStepFallbacks, 0u);
    params.EStepExternal = [&calls](const CPD::EStep::ExternalRequest& r) { return ReferenceExternal(r, calls); };
    std::vector<CPD::IterationTrace> trace;
    const auto device = CPD::Register(registered, source, params, [&](const CPD::IterationTrace& t) { trace.push_back(t); });
    ASSERT_TRUE(device.Succeeded());
    EXPECT_EQ(device.Backend, "gpu_vulkan_fp32_dense");
    EXPECT_EQ(device.EStepFallbacks, 0u);
    EXPECT_EQ(calls, std::size_t(cpu.EStepFallbacks));
    EXPECT_EQ(trace.front().EStep, CPD::EStepPolicy::Vulkan);
    EXPECT_LE(MaxTransformDifference(cpu, device), 1e-9);
}

TEST(CoherentPointDriftAccelerated, LowRankNonrigidConvergesToTheFullSolution)
{
    const auto source = Cloud(400, 3);
    const auto target = Bent(source);
    const CPD::Params full{.Method = CPD::Variant::Nonrigid, .MaxIterations = 40, .Tolerance = 0.0};
    const auto exact = CPD::Register(target, source, full);
    ASSERT_TRUE(exact.Succeeded());
    double previous = std::numeric_limits<double>::infinity();
    double previousKernel = std::numeric_limits<double>::infinity();
    for (const std::uint32_t rank : {5u, 20u, 60u})
    {
        CPD::Params lowRank = full;
        lowRank.LowRank = rank;
        const auto approx = CPD::Register(target, source, lowRank);
        ASSERT_TRUE(approx.Succeeded()) << rank;
        EXPECT_LE(approx.KernelRank, rank);
        EXPECT_GT(approx.KernelRank, 0u);
        const double difference = MaxPointDifference(exact, approx);
        EXPECT_LE(difference, previous) << rank;
        EXPECT_LE(approx.KernelApproximationError, previousKernel) << rank;
        std::printf("[lowrank] k=%u rank=%u kernelError=%.3e pointDelta=%.3e\n", rank, approx.KernelRank,
                    approx.KernelApproximationError, difference);
        previous = difference;
        previousKernel = approx.KernelApproximationError;
    }
    EXPECT_LE(previous, 2e-3);
}

TEST(CoherentPointDriftAccelerated, LowRankLiftsTheNonrigidSizeLimit)
{
    const auto source = Cloud(CPD::kMaxNonrigidSourcePoints + 100, 41);
    const auto target = Bent(source);
    const CPD::Params dense{.Method = CPD::Variant::Nonrigid, .MaxIterations = 3};
    EXPECT_EQ(CPD::Register(target, source, dense).State, CPD::Status::TooLarge);
    CPD::Params lowRank = dense;
    lowRank.LowRank = 30;
    lowRank.EStep = CPD::EStepPolicy::Auto;
    const auto result = CPD::Register(target, source, lowRank);
    ASSERT_TRUE(result.Succeeded()) << CPD::ToString(result.State);
    EXPECT_EQ(result.Iterations, 3u);
    EXPECT_EQ(result.KernelRank, 30u);
}

TEST(CoherentPointDriftAccelerated, InvalidAccelerationSettingsFailClosed)
{
    const auto source = Cloud(50, 1);
    for (const double tolerance : {0.0, 1.0, -1e-3, std::numeric_limits<double>::quiet_NaN()})
        EXPECT_EQ(CPD::Register(source, source, CPD::Params{.EStep = CPD::EStepPolicy::Truncated,
                                                            .EStepTolerance = tolerance}).State,
                  CPD::Status::InvalidParameters);
    EXPECT_EQ(CPD::Register(source, source, CPD::Params{.EStep = CPD::EStepPolicy(9)}).State,
              CPD::Status::InvalidParameters);
    for (const std::uint32_t landmarks : {0u, 1u, CPD::kMaxNystromLandmarks + 1u})
        EXPECT_EQ(CPD::Register(source, source, CPD::Params{.EStep = CPD::EStepPolicy::Nystrom,
                                                            .NystromLandmarks = landmarks}).State,
                  CPD::Status::InvalidParameters);
    for (const double limit : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()})
        EXPECT_EQ(CPD::Register(source, source, CPD::Params{.EStep = CPD::EStepPolicy::Nystrom,
                                                            .NystromErrorLimit = limit}).State,
                  CPD::Status::InvalidParameters);
    // The tolerance is ignored by the reference path.
    EXPECT_TRUE(CPD::Register(source, source, CPD::Params{.EStepTolerance = 0.0}).Succeeded());
    CPD::EStep::LowRankKernel kernel;
    const Soa points(source);
    EXPECT_FALSE(CPD::EStep::BuildLowRankGaussianKernel(points.View(), 0.0, 5, 1, kernel));
    EXPECT_FALSE(CPD::EStep::BuildLowRankGaussianKernel(points.View(), 1.0, 0, 1, kernel));
}
