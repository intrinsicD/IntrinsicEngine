// METHOD-050: Bayesian Coherent Point Drift (Hirose, TPAMI 2021) as a variant of the
// METHOD-015 solver, pinned per iteration to an independent NumPy implementation of Hirose's
// Algorithm 1 (MatchesAnIndependentImplementationOfAlgorithmOne).
// Frozen tolerances (unit-cube fixtures, noise 0.003):
//   similarity only (strong prior):  scale within 0.02, rotation within 0.02 rad
//   similarity + smooth deformation: RMS to the ground truth <= 0.02 (the split between
//                                    similarity and deformation is not identifiable)
//   30% clutter with omega 0.2:      RMS <= 0.03 and better than omega 0
//   subsampling 400 of 800:          RMS <= 0.005 on every source point (omega 0.1). Sparser
//                                    samples keep the shape but not point correspondences: each
//                                    sample settles on the centroid of its dense target cell.
//   accelerated parity:              dense E-step <= 1e-8 (rounding of the vectorized exp and the
//                                    block summation over ~40 EM iterations), low rank 60 <= 5e-3
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
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

    struct Fixture
    {
        std::vector<glm::vec3> Source, Target, Truth; // Truth[i]: image of Source[i]
    };

    // x = s R (y + v(y)) + t + noise with a smooth v of amplitude about 0.08.
    Fixture Deformed(std::vector<glm::vec3> source, double noise, std::uint32_t seed)
    {
        Fixture f;
        f.Source = std::move(source);
        const glm::dmat3 rotation(glm::rotate(glm::dmat4(1.0), 0.3, glm::normalize(glm::dvec3(0.4, 1.0, 0.2))));
        const double scale = 1.2;
        const glm::dvec3 translation{0.25, -0.1, 0.15};
        std::mt19937 random(seed);
        std::normal_distribution<double> gaussian(0.0, noise);
        for (const auto& p : f.Source)
        {
            const glm::dvec3 u = glm::dvec3(p) + glm::dvec3(0.0, 0.08 * std::sin(2.0 * p.x), 0.06 * std::cos(1.5 * p.y));
            const glm::dvec3 x = scale * (rotation * u) + translation;
            f.Truth.push_back(glm::vec3(x));
            f.Target.push_back(glm::vec3(x + glm::dvec3(gaussian(random), gaussian(random), gaussian(random))));
        }
        return f;
    }

    double Rms(const CPD::Result& r, const std::vector<glm::vec3>& truth)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < truth.size(); ++i)
        {
            const glm::dvec3 d = r.TransformedSource[i] - glm::dvec3(truth[i]);
            sum += glm::dot(d, d);
        }
        return std::sqrt(sum / double(truth.size()));
    }

    double MaxDifference(const CPD::Result& a, const CPD::Result& b)
    {
        double worst = 0.0;
        for (std::size_t i = 0; i < a.TransformedSource.size(); ++i)
            worst = std::max(worst, glm::length(a.TransformedSource[i] - b.TransformedSource[i]));
        return worst;
    }

    CPD::Params Bayesian()
    {
        CPD::Params params;
        params.Method = CPD::Variant::Bayesian;
        params.MaxIterations = 120;
        params.Lambda = 2.0;
        params.Beta = 1.0;
        return params;
    }
}

TEST(CoherentPointDriftBayesian, RecoversASimilarityUnderAStrongDeformationPrior)
{
    const auto source = Cloud(200, 21);
    const glm::dmat3 rotation(glm::rotate(glm::dmat4(1.0), 0.3, glm::normalize(glm::dvec3(0.4, 1.0, 0.2))));
    std::vector<glm::vec3> target;
    for (const auto& p : source) target.push_back(glm::vec3(1.2 * (rotation * glm::dvec3(p)) + glm::dvec3(0.25, -0.1, 0.15)));
    CPD::Params params = Bayesian();
    params.Lambda = 1e4;
    const auto result = CPD::Register(target, source, params);
    ASSERT_TRUE(result.Succeeded()) << CPD::ToString(result.State);
    EXPECT_NEAR(result.Scale, 1.2, 0.02);
    const glm::dmat3 relative = glm::transpose(rotation) * result.Rotation;
    EXPECT_LT(std::acos(std::clamp((relative[0][0] + relative[1][1] + relative[2][2] - 1.0) / 2.0, -1.0, 1.0)), 0.02);
}

TEST(CoherentPointDriftBayesian, FollowsASimilarityWithASmoothDeformation)
{
    const Fixture f = Deformed(Cloud(250, 1), 0.003, 2);
    std::vector<CPD::IterationTrace> trace;
    const auto result = CPD::Register(f.Target, f.Source, Bayesian(), [&](const CPD::IterationTrace& t) { trace.push_back(t); });
    ASSERT_TRUE(result.Succeeded()) << CPD::ToString(result.State);
    EXPECT_EQ(result.Method, CPD::Variant::Bayesian);
    EXPECT_EQ(result.TransformedSource.size(), f.Source.size());
    EXPECT_LE(Rms(result, f.Truth), 0.02);
    EXPECT_NEAR(glm::determinant(result.Rotation), 1.0, 1e-9);
    ASSERT_FALSE(trace.empty());
    EXPECT_LT(trace.back().Sigma2, trace.front().Sigma2);
}

TEST(CoherentPointDriftBayesian, OutlierWeightHandlesClutter)
{
    Fixture f = Deformed(Cloud(200, 5), 0.003, 6);
    std::mt19937 random(7);
    std::uniform_real_distribution<float> clutter(-1.8f, 1.8f);
    for (int k = 0; k < 60; ++k) f.Target.push_back({clutter(random), clutter(random), clutter(random)});
    CPD::Params robust = Bayesian();
    robust.OutlierWeight = 0.2;
    const auto withOutliers = CPD::Register(f.Target, f.Source, robust);
    const auto without = CPD::Register(f.Target, f.Source, Bayesian());
    ASSERT_TRUE(withOutliers.Succeeded() && without.Succeeded());
    EXPECT_LE(Rms(withOutliers, f.Truth), 0.03);
    EXPECT_LT(Rms(withOutliers, f.Truth), Rms(without, f.Truth));
}

TEST(CoherentPointDriftBayesian, FiniteKappaHandlesNonUniformDensity)
{
    // Three quarters of the source points crowd one corner.
    auto source = Cloud(240, 9);
    for (std::size_t i = 0; i < source.size(); i += 4) source[i] = 0.5f * source[i] + glm::vec3(0.4f, 0.4f, 0.0f);
    const Fixture f = Deformed(source, 0.003, 10);
    CPD::Params params = Bayesian();
    params.Kappa = 1.0;
    const auto result = CPD::Register(f.Target, f.Source, params);
    ASSERT_TRUE(result.Succeeded()) << CPD::ToString(result.State);
    EXPECT_LE(Rms(result, f.Truth), 0.03);
}

TEST(CoherentPointDriftBayesian, SubsampledRunsUpsampleTheDeformationToEveryPoint)
{
    // Register 400 farthest-point samples of the source, then evaluate the deformation's kernel
    // expansion at all 800 source points (full kernel and low rank, through the Nystroem extension).
    const Fixture f = Deformed(Cloud(800, 11), 0.002, 12);
    for (const std::uint32_t lowRank : {0u, 80u})
    {
        CPD::Params sampled = Bayesian();
        sampled.OutlierWeight = 0.1;
        sampled.SubsampleSource = 400;
        sampled.LowRank = lowRank;
        const auto upsampled = CPD::Register(f.Target, f.Source, sampled);
        ASSERT_TRUE(upsampled.Succeeded()) << lowRank;
        ASSERT_EQ(upsampled.TransformedSource.size(), f.Source.size());
        EXPECT_LE(Rms(upsampled, f.Truth), 0.005) << lowRank;
    }
}

TEST(CoherentPointDriftBayesian, MatchesAnIndependentImplementationOfAlgorithmOne)
{
    // Per-iteration sigma^2 and scale of ara/evidence/diagnostics/method050_bcpd_numpy_20260928/
    // bcpd_reference.py (lambda 2, beta 1, scale estimated, omega 0, kappa infinity, gamma 1, no
    // normalization) on its 60-point bend, without and with the posterior-variance terms. With them
    // the scale collapses toward 0 in both implementations (C114).
    const auto read = [](const char* name)
    {
        std::vector<glm::vec3> points;
        std::ifstream file(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "data" / "cpd" / name);
        double x = 0.0, y = 0.0, z = 0.0;
        while (file >> x >> y >> z) points.push_back(glm::vec3(float(x), float(y), float(z)));
        return points;
    };
    const auto source = read("bcpd_bend_source.txt"), target = read("bcpd_bend_target.txt");
    ASSERT_EQ(source.size(), 60u);
    ASSERT_EQ(target.size(), 60u);
    const std::vector<std::pair<double, double>> withoutVariances{
        {0.17219708619094717, 0.86321587352928586},
        {0.15071412582226754, 0.82906642847552825},
        {0.13110234778436036, 0.80947437374534637},
        {0.11223954888496192, 0.79802063779045196},
        {0.095782465433470562, 0.79129673899736186},
        {0.081667080601551645, 0.78742888729848326},
        {0.068650276733532231, 0.78550745681590073},
        {0.055288697985051864, 0.78514793537434668}};
    const std::vector<std::pair<double, double>> withVariances{
        {0.19277420635387874, 0.47451678497149369},
        {0.20258323769859418, 0.27130434162801043},
        {0.20906435691919192, 0.1543991838580589},
        {0.21392036691908695, 0.087556540539058453},
        {0.21674089371964023, 0.043423583375999955},
        {0.21768348018808456, 0.017578513240393463},
        {0.21786485534491182, 0.0064306768484834551},
        {0.21789056526368941, 0.0023016823808220013}};
    for (const bool variances : {false, true})
    {
        CPD::Params params = Bayesian();
        params.NormalizeInputs = false;
        params.Lambda = 2.0;
        params.Beta = 1.0;
        params.EstimateScale = true;
        params.PosteriorVarianceTerms = variances;
        params.MaxIterations = 8;
        params.Tolerance = 0.0;
        params.Sigma2Floor = 1e-12;
        std::vector<std::pair<double, double>> trace;
        (void)CPD::Register(target, source, params, [&](const CPD::IterationTrace& t)
                            { trace.emplace_back(t.Sigma2, glm::length(glm::dvec3(t.Transform[0]))); });
        const auto& expected = variances ? withVariances : withoutVariances;
        ASSERT_EQ(trace.size(), expected.size()) << variances;
        // The collapsing run amplifies rounding (float inputs, summation order) each iteration.
        const double relative = variances ? 1e-7 : 1e-9;
        for (std::size_t k = 0; k < expected.size(); ++k)
        {
            EXPECT_NEAR(trace[k].first, expected[k].first, relative * expected[k].first) << variances << " iteration " << k;
            EXPECT_NEAR(trace[k].second, expected[k].second, relative * expected[k].second) << variances << " iteration " << k;
        }
        if (variances) EXPECT_LT(trace.back().second, 0.01) << "the scale collapses with the variance terms";
    }
}

TEST(CoherentPointDriftBayesian, SubsampledLowRankResultIsValidBeforeTheFirstIteration)
{
    const Fixture f = Deformed(Cloud(300, 31), 0.002, 32);
    CPD::Params params = Bayesian();
    params.SubsampleSource = 100;
    params.LowRank = 20;
    CPD::Solver solver;
    ASSERT_EQ(solver.Initialize(f.Target, f.Source, params), CPD::Status::Success);
    // No coefficients yet: zero deformation, identity similarity after centering both sets.
    const auto before = solver.Current();
    ASSERT_EQ(before.TransformedSource.size(), f.Source.size());
    glm::dvec3 sourceMean{0.0}, targetMean{0.0};
    for (const auto& p : f.Source) sourceMean += glm::dvec3(p) / double(f.Source.size());
    for (const auto& p : f.Target) targetMean += glm::dvec3(p) / double(f.Target.size());
    for (std::size_t i = 0; i < f.Source.size(); ++i)
        ASSERT_LT(glm::length(before.TransformedSource[i] - (glm::dvec3(f.Source[i]) - sourceMean + targetMean)), 1e-5);
    solver.Run();
    EXPECT_TRUE(solver.Current().Succeeded());
}

TEST(CoherentPointDriftBayesian, AcceleratedPathsMatchTheReference)
{
    const Fixture f = Deformed(Cloud(250, 13), 0.003, 14);
    CPD::Params reference = Bayesian();
    reference.OutlierWeight = 0.05;
    reference.Kappa = 2.0;
    const auto exact = CPD::Register(f.Target, f.Source, reference);
    CPD::Params dense = reference;
    dense.EStep = CPD::EStepPolicy::Dense;
    dense.Threads = 3;
    const auto parallel = CPD::Register(f.Target, f.Source, dense);
    ASSERT_TRUE(exact.Succeeded() && parallel.Succeeded());
    EXPECT_EQ(exact.Iterations, parallel.Iterations);
    EXPECT_LE(MaxDifference(exact, parallel), 1e-8);

    CPD::Params truncated = reference;
    truncated.EStep = CPD::EStepPolicy::Truncated;
    truncated.EStepTolerance = 1e-9;
    const auto bounded = CPD::Register(f.Target, f.Source, truncated);
    ASSERT_TRUE(bounded.Succeeded());
    EXPECT_LE(bounded.EStepErrorBound, 1e-9);
    EXPECT_LE(MaxDifference(exact, bounded), 1e-5);

    CPD::Params lowRank = reference;
    lowRank.LowRank = 60;
    const auto approximate = CPD::Register(f.Target, f.Source, lowRank);
    ASSERT_TRUE(approximate.Succeeded());
    EXPECT_GT(approximate.KernelRank, 0u);
    EXPECT_LE(MaxDifference(exact, approximate), 5e-3);
}

TEST(CoherentPointDriftBayesian, InvalidSettingsFailClosedAndRunsAreDeterministic)
{
    const Fixture f = Deformed(Cloud(120, 15), 0.003, 16);
    const auto invalid = [&](auto&& edit)
    {
        CPD::Params p = Bayesian();
        edit(p);
        return CPD::Register(f.Target, f.Source, p).State;
    };
    EXPECT_EQ(invalid([](CPD::Params& p) { p.Gamma = 0.0; }), CPD::Status::InvalidParameters);
    EXPECT_EQ(invalid([](CPD::Params& p) { p.Kappa = std::numeric_limits<double>::quiet_NaN(); }),
              CPD::Status::InvalidParameters);
    EXPECT_EQ(invalid([](CPD::Params& p) { p.Kappa = -1.0; }), CPD::Status::InvalidParameters);
    EXPECT_EQ(invalid([](CPD::Params& p) { p.SubsampleSource = 2; }), CPD::Status::InvalidParameters);
    EXPECT_EQ(invalid([](CPD::Params& p) { p.SubsampleTarget = 3; }), CPD::Status::InvalidParameters);
    EXPECT_EQ(invalid([](CPD::Params& p) { p.Lambda = 0.0; }), CPD::Status::InvalidParameters);
    const auto large = Cloud(CPD::kMaxNonrigidSourcePoints + 1, 17);
    EXPECT_EQ(CPD::Register(large, large, Bayesian()).State, CPD::Status::TooLarge);
    std::vector<glm::vec3> broken = f.Source;
    broken[3].y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(CPD::Register(f.Target, broken, Bayesian()).State, CPD::Status::NonFiniteInput);

    const auto first = CPD::Register(f.Target, f.Source, Bayesian());
    const auto second = CPD::Register(f.Target, f.Source, Bayesian());
    ASSERT_TRUE(first.Succeeded());
    for (std::size_t i = 0; i < first.TransformedSource.size(); ++i)
        ASSERT_EQ(first.TransformedSource[i], second.TransformedSource[i]);
}
