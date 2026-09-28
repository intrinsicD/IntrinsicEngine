// METHOD-015: Coherent Point Drift CPU reference. Tolerances are frozen here for the
// fixtures below (unit-cube scale, Gaussian noise sigma 0.005):
//   rigid:  rotation <= 0.01 rad, translation <= 0.01, scale <= 0.01
//   affine: max matrix entry error <= 0.02, translation <= 0.02
//   nonrigid: mean landmark error <= 0.02 (displacement amplitude 0.1)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

import Geometry.Registration.CoherentPointDrift;
import Geometry.GaussianMixture;

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

    glm::dmat3 RotationAbout(glm::dvec3 axis, double angle)
    {
        return glm::dmat3(glm::rotate(glm::dmat4(1.0), angle, glm::normalize(axis)));
    }

    std::vector<glm::vec3> Apply(const std::vector<glm::vec3>& points, const glm::dmat3& linear, const glm::dvec3& t,
                                 double noise, std::uint32_t seed)
    {
        std::mt19937 random(seed);
        std::normal_distribution<double> gaussian(0.0, noise);
        std::vector<glm::vec3> out;
        for (const auto& p : points)
        {
            const glm::dvec3 q = linear * glm::dvec3(p) + t;
            out.push_back(glm::vec3(q + (noise > 0.0 ? glm::dvec3(gaussian(random), gaussian(random), gaussian(random))
                                                     : glm::dvec3(0.0))));
        }
        return out;
    }

    double RotationAngle(const glm::dmat3& a, const glm::dmat3& b)
    {
        const glm::dmat3 r = glm::transpose(a) * b;
        return std::acos(std::clamp((r[0][0] + r[1][1] + r[2][2] - 1.0) / 2.0, -1.0, 1.0));
    }

    double MaxEntryError(const glm::dmat3& a, const glm::dmat3& b)
    {
        double error = 0.0;
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r) error = std::max(error, std::abs(a[c][r] - b[c][r]));
        return error;
    }
}

TEST(CoherentPointDrift, RigidRecoversRotationScaleAndTranslationOnPartialNoisyData)
{
    const auto source = Cloud(400, 11);
    const glm::dmat3 rotation = RotationAbout({1.0, 2.0, 0.5}, 0.6);
    const double scale = 1.3;
    const glm::dvec3 translation{0.4, -0.2, 0.7};
    // The target sees 300 of the 400 source points (partial overlap), with noise.
    std::vector<glm::vec3> visible(source.begin() + 50, source.begin() + 350);
    const auto target = Apply(visible, scale * rotation, translation, 0.005, 3);

    const auto result = CPD::Register(target, source, CPD::Params{.OutlierWeight = 0.1});
    ASSERT_TRUE(result.Succeeded()) << CPD::ToString(result.State);
    EXPECT_EQ(result.Backend, "cpu_reference");
    EXPECT_NE(result.Stop, CPD::Termination::None);
    EXPECT_LE(RotationAngle(result.Rotation, rotation), 0.01);
    EXPECT_NEAR(result.Scale, scale, 0.01);
    EXPECT_LE(glm::length(result.Translation - translation), 0.01);
    EXPECT_NEAR(glm::determinant(result.Rotation), 1.0, 1e-9);
    // Transform and transformed points agree.
    const glm::dvec3 moved = glm::dvec3(result.Transform * glm::dvec4(glm::dvec3(source[7]), 1.0));
    EXPECT_LE(glm::length(moved - result.TransformedSource[7]), 1e-9);
    // The objective history decreases (EM monotonicity, up to rounding).
    for (std::size_t i = 1; i < result.ObjectiveHistory.size(); ++i)
        EXPECT_LE(result.ObjectiveHistory[i], result.ObjectiveHistory[i - 1] + 1e-9 * std::abs(result.ObjectiveHistory[i - 1]));
}

TEST(CoherentPointDrift, RigidWithoutScaleKeepsUnitScaleAndRejectsReflections)
{
    const auto source = Cloud(300, 5);
    const glm::dmat3 rotation = RotationAbout({0.0, 0.0, 1.0}, -0.8);
    const auto target = Apply(source, rotation, {1.0, 2.0, 3.0}, 0.0, 1);
    const auto result = CPD::Register(target, source, CPD::Params{.EstimateScale = false});
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Scale, 1.0);
    EXPECT_LE(RotationAngle(result.Rotation, rotation), 1e-4);

    // A thin slab mirrored through its own plane, started with a small sigma^2 so the
    // correspondences are right from the first step (CPD is local: from a wide start it
    // settles in a proper-rotation optimum, as an independent implementation confirms).
    auto slab = source;
    for (auto& p : slab) p.z *= 0.05f;
    const glm::dmat3 mirror{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    const auto mirrored = Apply(slab, mirror, {0.0, 0.0, 0.0}, 0.0, 1);
    const CPD::Params nearby{.InitialSigma2 = 1e-4, .EstimateScale = false};
    const auto proper = CPD::Register(mirrored, slab, nearby);
    ASSERT_TRUE(proper.Succeeded());
    EXPECT_NEAR(glm::determinant(proper.Rotation), 1.0, 1e-9);
    auto allowing = nearby;
    allowing.AllowReflection = true;
    const auto reflected = CPD::Register(mirrored, slab, allowing);
    ASSERT_TRUE(reflected.Succeeded());
    EXPECT_NEAR(glm::determinant(reflected.Rotation), -1.0, 1e-9);
    EXPECT_LE(MaxEntryError(reflected.Rotation, mirror), 1e-3);
}

TEST(CoherentPointDrift, OutlierWeightMakesRigidRobustToClutter)
{
    const auto source = Cloud(300, 21);
    const glm::dmat3 rotation = RotationAbout({0.3, 1.0, 0.2}, 0.4);
    const glm::dvec3 translation{0.3, 0.1, -0.2};
    auto target = Apply(source, rotation, translation, 0.005, 9);
    // 40% clutter concentrated on one side, far from the true surface.
    std::mt19937 random(77);
    std::uniform_real_distribution<float> clutter(1.5f, 3.0f);
    for (int i = 0; i < 120; ++i) target.push_back({clutter(random), clutter(random), clutter(random)});

    const auto robust = CPD::Register(target, source, CPD::Params{.OutlierWeight = 0.4, .EstimateScale = false});
    ASSERT_TRUE(robust.Succeeded());
    EXPECT_LE(RotationAngle(robust.Rotation, rotation), 0.01);
    EXPECT_LE(glm::length(robust.Translation - translation), 0.01);

    const auto naive = CPD::Register(target, source, CPD::Params{.OutlierWeight = 0.0, .EstimateScale = false});
    ASSERT_TRUE(naive.Succeeded());
    EXPECT_GT(glm::length(naive.Translation - translation), 0.05) << "without the uniform term the clutter pulls the fit";
}

TEST(CoherentPointDrift, AffineRecoversAGeneralLinearMap)
{
    const auto source = Cloud(400, 31);
    const glm::dmat3 linear{1.2, 0.1, -0.05, 0.2, 0.8, 0.1, -0.1, 0.15, 1.1};
    const glm::dvec3 translation{-0.3, 0.5, 0.2};
    const auto target = Apply(source, linear, translation, 0.005, 4);
    const auto result = CPD::Register(target, source, CPD::Params{.Method = CPD::Variant::Affine});
    ASSERT_TRUE(result.Succeeded()) << CPD::ToString(result.State);
    EXPECT_LE(MaxEntryError(result.Linear, linear), 0.02);
    EXPECT_LE(glm::length(result.Translation - translation), 0.02);
}

TEST(CoherentPointDrift, NonrigidFollowsASmoothDeformationAndStaysCoherent)
{
    // A 20 x 20 sheet bent by a smooth displacement of amplitude 0.1.
    std::vector<glm::vec3> source, deformed;
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x)
        {
            const glm::vec3 p{float(x) / 19.0f * 2.0f - 1.0f, float(y) / 19.0f * 2.0f - 1.0f, 0.0f};
            source.push_back(p);
            deformed.push_back(p + glm::vec3(0.1f * std::sin(2.0f * p.y), 0.0f, 0.1f * std::cos(1.5f * p.x)));
        }
    const auto result = CPD::Register(deformed, source,
                                      CPD::Params{.Method = CPD::Variant::Nonrigid, .MaxIterations = 200, .Beta = 2.0, .Lambda = 3.0});
    ASSERT_TRUE(result.Succeeded()) << CPD::ToString(result.State);
    double meanError = 0.0, initialError = 0.0;
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        meanError += glm::length(result.TransformedSource[i] - glm::dvec3(deformed[i]));
        initialError += glm::length(glm::dvec3(source[i]) - glm::dvec3(deformed[i]));
    }
    meanError /= double(source.size());
    initialError /= double(source.size());
    EXPECT_LE(meanError, 0.02) << "initial " << initialError;
    // Coherence: neighbouring displacements differ by far less than the displacement itself.
    double maxJump = 0.0;
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x + 1 < 20; ++x)
        {
            const std::size_t a = std::size_t(y * 20 + x), b = a + 1;
            const glm::dvec3 da = result.TransformedSource[a] - glm::dvec3(source[a]);
            const glm::dvec3 db = result.TransformedSource[b] - glm::dvec3(source[b]);
            maxJump = std::max(maxJump, glm::length(da - db));
        }
    EXPECT_LE(maxJump, 0.05);
}

// The CPD E-step is the posterior of an equal-weight isotropic Gaussian mixture (plus a
// uniform term). With w = 0 its first negative log-likelihood equals the GEOM-058 mixture
// log-likelihood of the same model.
TEST(CoherentPointDrift, ExpectationStepMatchesTheGaussianMixtureSurface)
{
    const auto source = Cloud(40, 51);
    const auto target = Apply(Cloud(60, 52), glm::dmat3(1.0), {0.1, 0.0, 0.0}, 0.0, 1);
    const double sigma2 = 0.07;
    double firstNll = 0.0;
    const auto result = CPD::Register(target, source,
                                      CPD::Params{.MaxIterations = 1, .InitialSigma2 = sigma2, .NormalizeInputs = false},
                                      [&](const CPD::IterationTrace& trace) { if (trace.Iteration == 0) firstNll = trace.NegativeLogLikelihood; });
    ASSERT_TRUE(result.Succeeded());
    Geometry::GaussianMixture::Model model;
    for (const auto& y : source)
    {
        model.Weights.push_back(1.0 / double(source.size()));
        model.Components.push_back({.Mean = glm::dvec3(y), .Covariance = glm::dmat3(sigma2)});
    }
    const auto logLikelihood = Geometry::GaussianMixture::LogLikelihood(model, target);
    ASSERT_TRUE(logLikelihood.has_value());
    EXPECT_NEAR(firstNll, -*logLikelihood, 1e-9 * std::abs(*logLikelihood));
}

TEST(CoherentPointDrift, RunsAreDeterministicObservedOrNotAndStepModeMatches)
{
    const auto source = Cloud(200, 61);
    const auto target = Apply(source, RotationAbout({1, 1, 0}, 0.3), {0.2, 0.2, 0.2}, 0.01, 2);
    const CPD::Params params{.OutlierWeight = 0.05};
    const auto first = CPD::Register(target, source, params);
    std::uint32_t observed = 0;
    const auto second = CPD::Register(target, source, params, [&](const CPD::IterationTrace&) { ++observed; });
    ASSERT_TRUE(first.Succeeded());
    EXPECT_EQ(observed, second.Iterations);
    EXPECT_EQ(first.Iterations, second.Iterations);
    EXPECT_EQ(first.Sigma2, second.Sigma2);
    EXPECT_EQ(first.ObjectiveHistory, second.ObjectiveHistory);
    for (std::size_t i = 0; i < first.TransformedSource.size(); ++i)
        EXPECT_EQ(first.TransformedSource[i], second.TransformedSource[i]);

    CPD::Solver solver;
    ASSERT_EQ(solver.Initialize(target, source, params), CPD::Status::Success);
    EXPECT_FALSE(solver.Finished());
    std::uint32_t steps = 0;
    while (solver.Step()) ++steps;
    const auto stepped = solver.Current();
    EXPECT_TRUE(solver.Finished());
    EXPECT_EQ(stepped.Sigma2, first.Sigma2);
    EXPECT_EQ(stepped.Transform, first.Transform);
}

TEST(CoherentPointDrift, TerminatesOnTheSigmaFloorAndTheIterationCap)
{
    const auto source = Cloud(100, 71);
    const auto same = CPD::Register(source, source, CPD::Params{.Tolerance = 0.0, .Sigma2Floor = 1e-8});
    ASSERT_TRUE(same.Succeeded());
    EXPECT_EQ(same.Stop, CPD::Termination::SigmaFloor);
    EXPECT_LE(same.Sigma2, 1e-6);

    const auto target = Apply(source, RotationAbout({0, 1, 0}, 0.5), {0.5, 0, 0}, 0.01, 3);
    const auto capped = CPD::Register(target, source, CPD::Params{.MaxIterations = 3, .Tolerance = 0.0});
    ASSERT_TRUE(capped.Succeeded());
    EXPECT_EQ(capped.Stop, CPD::Termination::IterationCap);
    EXPECT_EQ(capped.Iterations, 3u);
    EXPECT_EQ(capped.Sigma2History.size(), 3u);
}

TEST(CoherentPointDrift, InvalidInputsFailClosed)
{
    const auto cloud = Cloud(50, 81);
    EXPECT_EQ(CPD::Register({}, cloud, {}).State, CPD::Status::EmptyInput);
    EXPECT_EQ(CPD::Register(cloud, {}, {}).State, CPD::Status::EmptyInput);
    auto broken = cloud;
    broken[3].y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(CPD::Register(cloud, broken, {}).State, CPD::Status::NonFiniteInput);
    EXPECT_EQ(CPD::Register(cloud, cloud, CPD::Params{.OutlierWeight = 1.0}).State, CPD::Status::InvalidParameters);
    EXPECT_EQ(CPD::Register(cloud, cloud, CPD::Params{.OutlierWeight = -0.1}).State, CPD::Status::InvalidParameters);
    EXPECT_EQ(CPD::Register(cloud, cloud, CPD::Params{.MaxIterations = 0}).State, CPD::Status::InvalidParameters);
    EXPECT_EQ(CPD::Register(cloud, cloud, CPD::Params{.Method = CPD::Variant::Nonrigid, .Beta = 0.0}).State,
              CPD::Status::InvalidParameters);
    const std::vector<glm::vec3> huge(CPD::kMaxNonrigidSourcePoints + 1, glm::vec3(0.0f));
    EXPECT_EQ(CPD::Register(cloud, huge, CPD::Params{.Method = CPD::Variant::Nonrigid}).State, CPD::Status::TooLarge);

    // A planar source leaves the affine map undetermined along the normal.
    std::vector<glm::vec3> planar;
    for (int i = 0; i < 64; ++i) planar.push_back({float(i % 8), float(i / 8), 0.0f});
    const auto affine = CPD::Register(cloud, planar, CPD::Params{.Method = CPD::Variant::Affine});
    EXPECT_EQ(affine.State, CPD::Status::SingularSystem);
    EXPECT_TRUE(affine.TransformedSource.empty()) << "failures publish no geometry";
}
