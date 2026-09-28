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
    for (const CPD::EStepPolicy policy : {CPD::EStepPolicy::Dense, CPD::EStepPolicy::Truncated, CPD::EStepPolicy::Auto})
    {
        std::vector<CPD::Result> runs;
        for (const std::uint32_t threads : {1u, 3u, 8u})
            runs.push_back(CPD::Register(target, source, CPD::Params{.OutlierWeight = 0.05, .MaxIterations = 40,
                                                                     .EStep = policy, .Threads = threads}));
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
    // The tolerance is ignored by the reference path.
    EXPECT_TRUE(CPD::Register(source, source, CPD::Params{.EStepTolerance = 0.0}).Succeeded());
    CPD::EStep::LowRankKernel kernel;
    const Soa points(source);
    EXPECT_FALSE(CPD::EStep::BuildLowRankGaussianKernel(points.View(), 0.0, 5, 1, kernel));
    EXPECT_FALSE(CPD::EStep::BuildLowRankGaussianKernel(points.View(), 1.0, 0, 1, kernel));
}
