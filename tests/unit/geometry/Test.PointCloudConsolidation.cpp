#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <limits>
#include <span>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry.PointCloud;
import Geometry.PointCloud.Consolidation;
import Geometry.GaussianMixture;

namespace
{
    namespace Consolidation = Geometry::PointCloud::Consolidation;
    namespace GMM = Geometry::GaussianMixture;

    [[nodiscard]] Consolidation::Params ReferenceParams()
    {
        return Consolidation::Params{
            .Method = Consolidation::WlopStrategy{},
            .SupportRadius = 0.65,
            .RepulsionWeight = 0.2,
            .MaxIterations = 40u,
            .ConvergenceTolerance = 2.0e-4,
            .TargetPointCount = 0u,
            .Seed = 17u,
        };
    }

    [[nodiscard]] Consolidation::Params ClopParams(
        const std::uint32_t componentCount)
    {
        auto params = ReferenceParams();
        params.Method = Consolidation::ClopStrategy{
            .MixtureComponentCount = componentCount,
            .MixtureMaxIterations = 100u,
            .MixtureRelativeTolerance = 1.0e-6,
            .CovarianceFloor = 1.0e-5,
        };
        return params;
    }

    struct AnalyticTerm
    {
        double Weight;
        double Sigma;
    };

    [[nodiscard]] glm::vec3 AnalyticClopAttraction(
        const GMM::Model& model,
        const glm::vec3 query,
        const double supportRadius,
        const std::span<const AnalyticTerm> terms)
    {
        glm::dvec3 numerator{0.0};
        double denominator = 0.0;
        for (std::size_t component = 0u;
             component < model.Components.size(); ++component)
        {
            for (const AnalyticTerm term : terms)
            {
                const double scaledSigma = term.Sigma * supportRadius;
                const double variance = scaledSigma * scaledSigma;
                const glm::dmat3 lambda =
                    model.Components[component].Covariance +
                    glm::dmat3{variance};
                const glm::dmat3 inverse = glm::inverse(lambda);
                const glm::dvec3 delta =
                    model.Components[component].Mean - glm::dvec3(query);
                const double weight = model.Weights[component] *
                    term.Weight * scaledSigma * scaledSigma * scaledSigma /
                    std::sqrt(glm::determinant(lambda)) *
                    std::exp(-0.5 * glm::dot(delta, inverse * delta));
                numerator += weight *
                    (glm::dvec3(query) + variance * inverse * delta);
                denominator += weight;
            }
        }
        return glm::vec3(numerator / denominator);
    }

    [[nodiscard]] std::vector<glm::vec3> NoisyPlane(
        const int side = 7)
    {
        std::vector<glm::vec3> points{};
        points.reserve(static_cast<std::size_t>(side * side));
        for (int y = 0; y < side; ++y)
        {
            for (int x = 0; x < side; ++x)
            {
                const float noise =
                    ((x * 13 + y * 7) % 5 - 2) * 0.025f;
                points.emplace_back(
                    (static_cast<float>(x) - 0.5f * (side - 1)) * 0.2f,
                    (static_cast<float>(y) - 0.5f * (side - 1)) * 0.2f,
                    noise);
            }
        }
        return points;
    }

    [[nodiscard]] std::vector<glm::vec3> NoisySphere()
    {
        std::vector<glm::vec3> points{};
        constexpr double pi = 3.14159265358979323846;
        for (int latitude = 1; latitude <= 5; ++latitude)
        {
            const double phi = pi * latitude / 6.0;
            for (int longitude = 0; longitude < 12; ++longitude)
            {
                const double theta = 2.0 * pi * longitude / 12.0;
                const std::size_t index = points.size();
                const double noise =
                    (static_cast<int>((index * 11u) % 7u) - 3) * 0.012;
                const double radius = 1.0 + noise;
                points.emplace_back(
                    static_cast<float>(radius * std::sin(phi) *
                                       std::cos(theta)),
                    static_cast<float>(radius * std::sin(phi) *
                                       std::sin(theta)),
                    static_cast<float>(radius * std::cos(phi)));
            }
        }
        return points;
    }

    struct DihedralFixture
    {
        std::vector<glm::vec3> Positions{};
        std::vector<glm::vec3> Normals{};
        std::size_t PointsPerSide{0u};
        int RadialCount{0};
    };

    [[nodiscard]] DihedralFixture NoisyDihedral(
        const int radialCount = 5,
        const int creaseCount = 5)
    {
        DihedralFixture fixture{};
        constexpr double angle = 1.0471975511965976; // 60 degrees.
        const std::array<glm::vec3, 2u> tangents{
            glm::vec3{-1.0f, 0.0f, 0.0f},
            glm::vec3{
                static_cast<float>(std::cos(angle)), 0.0f,
                static_cast<float>(std::sin(angle))},
        };
        const std::array<glm::vec3, 2u> normals{
            glm::vec3{0.0f, 0.0f, 1.0f},
            glm::vec3{
                static_cast<float>(-std::sin(angle)), 0.0f,
                static_cast<float>(std::cos(angle))},
        };
        fixture.PointsPerSide = static_cast<std::size_t>(
            radialCount * creaseCount);
        fixture.RadialCount = radialCount;
        fixture.Positions.reserve(2u * fixture.PointsPerSide);
        fixture.Normals.reserve(2u * fixture.PointsPerSide);
        for (std::size_t side = 0u; side < 2u; ++side)
        {
            for (int y = 0; y < creaseCount; ++y)
            {
                for (int radial = 0; radial < radialCount; ++radial)
                {
                    const float distance = 0.08f + 0.13f * radial;
                    const float creasePosition =
                        (static_cast<float>(y) -
                         0.5f * static_cast<float>(creaseCount - 1)) *
                        0.16f;
                    const int noiseCode =
                        (radial * 11 + y * 7 + static_cast<int>(side) * 3) %
                            5 -
                        2;
                    const float noise = 0.0075f * noiseCode;
                    fixture.Positions.push_back(
                        distance * tangents[side] +
                        glm::vec3{0.0f, creasePosition, 0.0f} +
                        noise * normals[side]);
                    fixture.Normals.push_back(normals[side]);
                }
            }
        }
        return fixture;
    }

    [[nodiscard]] double MeanExpectedPlaneError(
        const std::span<const glm::vec3> points,
        const std::span<const glm::vec3> expectedNormals)
    {
        EXPECT_EQ(points.size(), expectedNormals.size());
        if (points.size() != expectedNormals.size() || points.empty())
            return std::numeric_limits<double>::infinity();
        double sum = 0.0;
        for (std::size_t i = 0u; i < points.size(); ++i)
        {
            sum += std::abs(glm::dot(
                glm::dvec3(points[i]), glm::dvec3(expectedNormals[i])));
        }
        return sum / static_cast<double>(points.size());
    }

    [[nodiscard]] double MeanPlaneError(
        const std::span<const glm::vec3> points)
    {
        double sum = 0.0;
        for (const glm::vec3 point : points)
            sum += std::abs(static_cast<double>(point.z));
        return sum / static_cast<double>(points.size());
    }

    [[nodiscard]] double MeanSphereError(
        const std::span<const glm::vec3> points)
    {
        double sum = 0.0;
        for (const glm::vec3 point : points)
            sum += std::abs(static_cast<double>(glm::length(point)) - 1.0);
        return sum / static_cast<double>(points.size());
    }

    [[nodiscard]] double MinimumPairwiseDistance(
        const std::span<const glm::vec3> points)
    {
        double minimum = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0u; i < points.size(); ++i)
        {
            for (std::size_t j = i + 1u; j < points.size(); ++j)
            {
                minimum = std::min(
                    minimum,
                    static_cast<double>(glm::distance(points[i], points[j])));
            }
        }
        return minimum;
    }

    [[nodiscard]] double MaximumNearestNeighborDistance(
        const std::span<const glm::vec3> points)
    {
        double maximum = 0.0;
        for (std::size_t i = 0u; i < points.size(); ++i)
        {
            double nearest = std::numeric_limits<double>::infinity();
            for (std::size_t j = 0u; j < points.size(); ++j)
            {
                if (i == j)
                    continue;
                nearest = std::min(
                    nearest,
                    static_cast<double>(glm::distance(points[i], points[j])));
            }
            maximum = std::max(maximum, nearest);
        }
        return maximum;
    }

    void ExpectFiniteOutput(const Consolidation::Result& result)
    {
        ASSERT_FALSE(result.Positions.empty());
        for (const glm::vec3 point : result.Positions)
        {
            EXPECT_TRUE(std::isfinite(point.x));
            EXPECT_TRUE(std::isfinite(point.y));
            EXPECT_TRUE(std::isfinite(point.z));
        }
    }

    struct VectorDelta
    {
        double Rms{0.0};
        double Linf{0.0};
    };

    [[nodiscard]] VectorDelta MeasureDelta(
        const std::span<const glm::vec3> reference,
        const std::span<const glm::vec3> candidate)
    {
        EXPECT_EQ(reference.size(), candidate.size());
        if (reference.size() != candidate.size() || reference.empty())
            return {};
        double squaredSum = 0.0;
        double maximum = 0.0;
        for (std::size_t i = 0u; i < reference.size(); ++i)
        {
            const glm::dvec3 delta =
                glm::dvec3(reference[i]) - glm::dvec3(candidate[i]);
            const double squared = glm::dot(delta, delta);
            squaredSum += squared;
            maximum = std::max(maximum, std::sqrt(squared));
        }
        return VectorDelta{
            std::sqrt(squaredSum / static_cast<double>(reference.size())),
            maximum,
        };
    }

    void ExpectCandidateParity(
        const std::span<const glm::vec3> positions,
        const std::span<const glm::vec3> normals,
        const Consolidation::Params& params)
    {
        const auto reference = normals.empty()
            ? Consolidation::Consolidate(positions, params)
            : Consolidation::Consolidate(positions, normals, params);
        const auto candidate = normals.empty()
            ? Consolidation::Validation::ConsolidateCpuOptimizedCandidate(
                positions, params)
            : Consolidation::Validation::ConsolidateCpuOptimizedCandidate(
                positions, normals, params);
        const auto repeated = normals.empty()
            ? Consolidation::Validation::ConsolidateCpuOptimizedCandidate(
                positions, params)
            : Consolidation::Validation::ConsolidateCpuOptimizedCandidate(
                positions, normals, params);

        EXPECT_EQ(reference.Diagnostics.Implementation,
                  Consolidation::kCpuReferenceImplementation);
        EXPECT_EQ(candidate.Diagnostics.Implementation,
                  Consolidation::Validation::
                      kOptimizedCandidateImplementation);
        ASSERT_EQ(candidate.State, reference.State)
            << "reference=" << Consolidation::DebugName(reference.State)
            << " candidate=" << Consolidation::DebugName(candidate.State);
        EXPECT_EQ(repeated.State, candidate.State);
        EXPECT_EQ(repeated.Positions, candidate.Positions);
        EXPECT_EQ(repeated.Normals, candidate.Normals);
        EXPECT_EQ(candidate.Positions.size(), reference.Positions.size());
        EXPECT_EQ(candidate.Normals.size(), reference.Normals.size());

        const VectorDelta positionsDelta = MeasureDelta(
            reference.Positions, candidate.Positions);
        const VectorDelta normalsDelta = MeasureDelta(
            reference.Normals, candidate.Normals);
        EXPECT_LE(positionsDelta.Rms, 1.0e-6);
        EXPECT_LE(positionsDelta.Linf, 2.0e-6);
        EXPECT_LE(normalsDelta.Rms, 1.0e-6);
        EXPECT_LE(normalsDelta.Linf, 2.0e-6);
    }
}

TEST(PointCloudConsolidation, OptimizedCandidatesMatchReferenceAndAreDeterministic)
{
    const auto plane = NoisyPlane(5);

    {
        auto lop = ReferenceParams();
        lop.Method = Consolidation::LopStrategy{};
        lop.MaxIterations = 2u;
        lop.ConvergenceTolerance = 1.0;
        lop.TargetPointCount = 16u;
        SCOPED_TRACE("lop");
        ExpectCandidateParity(plane, {}, lop);
    }

    {
        auto wlop = ReferenceParams();
        wlop.MaxIterations = 2u;
        wlop.ConvergenceTolerance = 1.0;
        wlop.TargetPointCount = 16u;
        SCOPED_TRACE("wlop");
        ExpectCandidateParity(plane, {}, wlop);
    }

    {
        auto clop = ClopParams(5u);
        clop.MaxIterations = 1u;
        clop.ConvergenceTolerance = 1.0;
        clop.TargetPointCount = 16u;
        SCOPED_TRACE("clop");
        ExpectCandidateParity(plane, {}, clop);
    }

    const DihedralFixture dihedral = NoisyDihedral(3, 3);
    auto ear = ReferenceParams();
    ear.Method = Consolidation::EarStrategy{
        .NormalSource =
            Consolidation::NormalSourcePolicy::RequireAuthored,
        .NormalAngleRadians = 3.14159265358979323846 / 12.0,
        .EdgeSensitivity = 5.0,
        .NormalRefinementRounds = 2u,
    };
    ear.SupportRadius = 0.65;
    ear.RepulsionWeight = 0.1;
    ear.MaxIterations = 2u;
    ear.ConvergenceTolerance = 1.0;
    ear.TargetPointCount = dihedral.Positions.size() + 2u;
    ear.MaxOutputPointCount = ear.TargetPointCount;
    {
        SCOPED_TRACE("ear");
        ExpectCandidateParity(dihedral.Positions, dihedral.Normals, ear);
    }
}

TEST(PointCloudConsolidation, OptimizedCandidateFailureMatchesReference)
{
    const std::vector<glm::vec3> sparse{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
    };
    auto params = ReferenceParams();
    params.SupportRadius = 0.25;
    const auto reference = Consolidation::Consolidate(sparse, params);
    const auto candidate =
        Consolidation::Validation::ConsolidateCpuOptimizedCandidate(
            sparse, params);
    EXPECT_EQ(reference.State, Consolidation::Status::EmptyNeighborhood);
    EXPECT_EQ(candidate.State, reference.State);
    EXPECT_TRUE(reference.Positions.empty());
    EXPECT_TRUE(candidate.Positions.empty());
    EXPECT_EQ(candidate.Diagnostics.Implementation,
              Consolidation::Validation::kOptimizedCandidateImplementation);
}

TEST(PointCloudConsolidation, WlopDenoisesPlaneAndSphere)
{
    const auto plane = NoisyPlane();
    auto planeParams = ReferenceParams();
    planeParams.RepulsionWeight = 0.0;
    planeParams.ConvergenceTolerance = 1.0;
    planeParams.TargetPointCount = 25u;
    planeParams.MaxIterations = 1u;
    const auto projectedPlane =
        Consolidation::Consolidate(plane, planeParams);
    ASSERT_TRUE(projectedPlane.Succeeded())
        << Consolidation::DebugName(projectedPlane.State);
    EXPECT_LT(MeanPlaneError(projectedPlane.Positions),
              MeanPlaneError(plane));
    // Frozen bound: the 0.0301 raw fixture must fall below 0.024 after the
    // deterministic initializer plus one reference iteration.
    EXPECT_LT(MeanPlaneError(projectedPlane.Positions), 0.024);

    const auto sphere = NoisySphere();
    auto sphereParams = ReferenceParams();
    sphereParams.SupportRadius = 0.9;
    sphereParams.RepulsionWeight = 0.0;
    sphereParams.MaxIterations = 50u;
    sphereParams.ConvergenceTolerance = 2.0e-3;
    const auto projectedSphere =
        Consolidation::Consolidate(sphere, sphereParams);
    ASSERT_TRUE(projectedSphere.Succeeded())
        << Consolidation::DebugName(projectedSphere.State);
    EXPECT_LT(MeanSphereError(projectedSphere.Positions),
              MeanSphereError(sphere));
    EXPECT_LT(MeanSphereError(projectedSphere.Positions), 0.025);
}

TEST(PointCloudConsolidation, AnisotropicWlopPreservesDihedralContrast)
{
    const DihedralFixture fixture = NoisyDihedral();
    auto isotropicParams = ReferenceParams();
    isotropicParams.SupportRadius = 0.22;
    isotropicParams.RepulsionWeight = 0.0;
    isotropicParams.MaxIterations = 3u;
    isotropicParams.ConvergenceTolerance = 0.0;
    const auto isotropic = Consolidation::Consolidate(
        fixture.Positions, fixture.Normals, isotropicParams);
    ASSERT_FALSE(isotropic.Positions.empty())
        << Consolidation::DebugName(isotropic.State);

    auto anisotropicParams = isotropicParams;
    anisotropicParams.Method = Consolidation::WlopStrategy{
        .Weighting = Consolidation::WeightingMode::Anisotropic,
        .NormalSource = Consolidation::NormalSourcePolicy::RequireAuthored,
        .NormalAngleRadians = 3.14159265358979323846 / 12.0,
        .NormalRefinementRounds = 3u,
    };
    const auto anisotropic = Consolidation::Consolidate(
        fixture.Positions, fixture.Normals, anisotropicParams);
    ASSERT_FALSE(anisotropic.Positions.empty())
        << Consolidation::DebugName(anisotropic.State);
    ASSERT_EQ(anisotropic.Normals.size(), anisotropic.Positions.size());

    const double inputError = MeanExpectedPlaneError(
        fixture.Positions, fixture.Normals);
    const double isotropicError = MeanExpectedPlaneError(
        isotropic.Positions, fixture.Normals);
    const double anisotropicError = MeanExpectedPlaneError(
        anisotropic.Positions, fixture.Normals);
    EXPECT_LT(anisotropicError, inputError);
    // Frozen contrast bound for this scale-normalized 60-degree fixture:
    // replacing isotropic attraction reduces expected-plane error by at
    // least 15 percent after the same three fixed-order iterations.
    EXPECT_LT(anisotropicError, isotropicError * 0.85)
        << "input=" << inputError << " isotropic=" << isotropicError
        << " anisotropic=" << anisotropicError;

    double flatInputError = 0.0;
    double flatOutputError = 0.0;
    std::size_t flatCount = 0u;
    for (std::size_t i = 0u; i < fixture.Positions.size(); ++i)
    {
        if (static_cast<int>(i % fixture.PointsPerSide) %
                fixture.RadialCount < 2)
        {
            continue;
        }
        flatInputError += std::abs(glm::dot(
            glm::dvec3(fixture.Positions[i]),
            glm::dvec3(fixture.Normals[i])));
        flatOutputError += std::abs(glm::dot(
            glm::dvec3(anisotropic.Positions[i]),
            glm::dvec3(fixture.Normals[i])));
        ++flatCount;
    }
    ASSERT_GT(flatCount, 0u);
    EXPECT_LT(flatOutputError / static_cast<double>(flatCount),
              flatInputError / static_cast<double>(flatCount));
    EXPECT_TRUE(anisotropic.Diagnostics.UsedAnisotropicWeighting);
    EXPECT_TRUE(anisotropic.Diagnostics.UsedAuthoredNormals);
    EXPECT_EQ(anisotropic.Diagnostics.NormalRefinementIterations, 3u);

    glm::dvec3 firstNormal{0.0};
    glm::dvec3 secondNormal{0.0};
    for (std::size_t i = 0u; i < fixture.PointsPerSide; ++i)
    {
        firstNormal += glm::dvec3(anisotropic.Normals[i]);
        secondNormal += glm::dvec3(
            anisotropic.Normals[i + fixture.PointsPerSide]);
    }
    firstNormal = glm::normalize(firstNormal);
    secondNormal = glm::normalize(secondNormal);
    const double retainedAngle = std::acos(std::clamp(
        glm::dot(firstNormal, secondNormal), -1.0, 1.0));
    EXPECT_NEAR(retainedAngle, 1.0471975511965976, 1.0e-5);
}

TEST(PointCloudConsolidation, EarUpsamplesTowardDihedralFeature)
{
    const DihedralFixture fixture = NoisyDihedral(3, 3);
    auto params = ReferenceParams();
    params.Method = Consolidation::EarStrategy{
        .NormalSource = Consolidation::NormalSourcePolicy::RequireAuthored,
        .NormalAngleRadians = 3.14159265358979323846 / 12.0,
        .EdgeSensitivity = 5.0,
        .NormalRefinementRounds = 3u,
    };
    params.SupportRadius = 0.65;
    params.RepulsionWeight = 0.1;
    params.MaxIterations = 3u;
    params.ConvergenceTolerance = 1.0;
    params.TargetPointCount = fixture.Positions.size() + 6u;
    params.MaxOutputPointCount = params.TargetPointCount;
    auto firstFuture = std::async(std::launch::async, [&]
    {
        return Consolidation::Consolidate(
            fixture.Positions, fixture.Normals, params);
    });
    auto secondFuture = std::async(std::launch::async, [&]
    {
        return Consolidation::Consolidate(
            fixture.Positions, fixture.Normals, params);
    });
    const auto result = Consolidation::Consolidate(
        fixture.Positions, fixture.Normals, params);
    const auto repeated = firstFuture.get();
    const auto concurrent = secondFuture.get();
    ASSERT_TRUE(result.Succeeded())
        << Consolidation::DebugName(result.State);
    ASSERT_EQ(repeated.State, result.State);
    EXPECT_EQ(repeated.Positions, result.Positions);
    EXPECT_EQ(repeated.Normals, result.Normals);
    EXPECT_EQ(concurrent.State, result.State);
    EXPECT_EQ(concurrent.Positions, result.Positions);
    EXPECT_EQ(concurrent.Normals, result.Normals);
    ASSERT_EQ(result.Positions.size(), params.TargetPointCount);
    ASSERT_EQ(result.Normals.size(), result.Positions.size());
    EXPECT_EQ(result.Diagnostics.InsertedPointCount, 6u);
    EXPECT_GT(result.Diagnostics.EdgePriorityEvaluations, 0u);
    EXPECT_EQ(result.Diagnostics.Strategy, Consolidation::StrategyKind::Ear);

    std::size_t nearCrease = 0u;
    for (std::size_t i = fixture.Positions.size();
         i < result.Positions.size(); ++i)
    {
        const double distanceToCrease = std::hypot(
            static_cast<double>(result.Positions[i].x),
            static_cast<double>(result.Positions[i].z));
        if (distanceToCrease < 0.30)
            ++nearCrease;
    }
    EXPECT_GE(nearCrease, 4u);
    EXPECT_LT(MaximumNearestNeighborDistance(result.Positions), 0.30);
    EXPECT_GT(MinimumPairwiseDistance(result.Positions), 1.0e-5);
}

TEST(PointCloudConsolidation, AnisotropicNormalPolicyIsExplicitAndImmutable)
{
    const auto points = NoisyPlane(5);
    auto params = ReferenceParams();
    params.Method = Consolidation::WlopStrategy{
        .Weighting = Consolidation::WeightingMode::Anisotropic,
        .NormalSource = Consolidation::NormalSourcePolicy::AuthoredOrEstimate,
        .NormalRefinementRounds = 1u,
    };
    params.MaxIterations = 1u;
    params.ConvergenceTolerance = 1.0;
    const auto estimated = Consolidation::Consolidate(points, params);
    const auto estimatedAgain = Consolidation::Consolidate(points, params);
    ASSERT_TRUE(estimated.Succeeded())
        << Consolidation::DebugName(estimated.State);
    ASSERT_EQ(estimatedAgain.State, estimated.State);
    EXPECT_EQ(estimatedAgain.Positions, estimated.Positions);
    EXPECT_EQ(estimatedAgain.Normals, estimated.Normals);
    EXPECT_TRUE(estimated.Diagnostics.EstimatedNormals);
    EXPECT_FALSE(estimated.Diagnostics.UsedAuthoredNormals);
    EXPECT_EQ(estimated.Normals.size(), points.size());

    std::get<Consolidation::WlopStrategy>(params.Method).NormalSource =
        Consolidation::NormalSourcePolicy::RequireAuthored;
    const auto required = Consolidation::Consolidate(points, params);
    EXPECT_EQ(required.State, Consolidation::Status::NormalsRequired);
    EXPECT_TRUE(required.Positions.empty());

    Geometry::PointCloud::Cloud cloud{};
    cloud.EnableNormals();
    std::vector<glm::vec3> authored{};
    authored.reserve(points.size());
    for (const glm::vec3 point : points)
    {
        const auto handle = cloud.AddPoint(point);
        cloud.Normal(handle) = {0.0f, 0.0f, 2.0f};
        authored.push_back(cloud.Normal(handle));
    }
    const auto withAuthored = Consolidation::Consolidate(cloud, params);
    ASSERT_TRUE(withAuthored.Succeeded())
        << Consolidation::DebugName(withAuthored.State);
    EXPECT_TRUE(withAuthored.Diagnostics.UsedAuthoredNormals);
    EXPECT_FALSE(withAuthored.Diagnostics.EstimatedNormals);
    EXPECT_EQ(std::vector<glm::vec3>(
                  cloud.Normals().begin(), cloud.Normals().end()),
              authored);
    for (const glm::vec3 normal : withAuthored.Normals)
        EXPECT_NEAR(glm::length(normal), 1.0f, 1.0e-6f);
}

TEST(PointCloudConsolidation, EarAndAnisotropicFailuresPublishNoPayload)
{
    const DihedralFixture fixture = NoisyDihedral(3, 3);
    auto params = ReferenceParams();
    params.Method = Consolidation::EarStrategy{
        .NormalSource = Consolidation::NormalSourcePolicy::RequireAuthored,
        .NormalRefinementRounds = 1u,
    };
    params.MaxIterations = 1u;
    params.ConvergenceTolerance = 1.0;
    const auto expectFailure = [&fixture](
        const Consolidation::Params& invalid,
        const Consolidation::Status expected,
        const std::span<const glm::vec3> normals)
    {
        const auto result = Consolidation::Consolidate(
            fixture.Positions, normals, invalid);
        EXPECT_EQ(result.State, expected)
            << Consolidation::DebugName(result.State);
        EXPECT_TRUE(result.Positions.empty());
        EXPECT_TRUE(result.Normals.empty());
    };

    auto invalid = params;
    std::get<Consolidation::EarStrategy>(invalid.Method)
        .NormalAngleRadians = 0.0;
    expectFailure(invalid, Consolidation::Status::InvalidNormalAngle,
                  fixture.Normals);

    invalid = params;
    std::get<Consolidation::EarStrategy>(invalid.Method)
        .EdgeSensitivity = -1.0;
    expectFailure(invalid, Consolidation::Status::InvalidEdgeSensitivity,
                  fixture.Normals);

    invalid = params;
    std::get<Consolidation::EarStrategy>(invalid.Method)
        .NormalRefinementRounds = 0u;
    expectFailure(
        invalid, Consolidation::Status::InvalidNormalRefinementRounds,
        fixture.Normals);

    invalid = params;
    invalid.TargetPointCount = fixture.Positions.size() + 1u;
    invalid.MaxOutputPointCount = fixture.Positions.size();
    expectFailure(invalid, Consolidation::Status::InvalidTargetCount,
                  fixture.Normals);

    std::vector<glm::vec3> invalidNormals = fixture.Normals;
    invalidNormals[0] = {0.0f, 0.0f, 0.0f};
    expectFailure(params, Consolidation::Status::InvalidNormals,
                  invalidNormals);
    invalidNormals = fixture.Normals;
    invalidNormals[0].x = std::numeric_limits<float>::quiet_NaN();
    expectFailure(params, Consolidation::Status::InvalidNormals,
                  invalidNormals);
    invalidNormals = fixture.Normals;
    invalidNormals[4] = -invalidNormals[4];
    expectFailure(params, Consolidation::Status::InvalidNormals,
                  invalidNormals);
    expectFailure(
        params, Consolidation::Status::InvalidNormals,
        std::span<const glm::vec3>{fixture.Normals.data(),
                                   fixture.Normals.size() - 1u});

    invalid = params;
    std::get<Consolidation::EarStrategy>(invalid.Method).NormalSource =
        Consolidation::NormalSourcePolicy::AuthoredOrEstimate;
    const std::vector<glm::vec3> coincident(
        6u, glm::vec3{0.0f, 0.0f, 0.0f});
    const auto degenerate = Consolidation::Consolidate(coincident, invalid);
    EXPECT_EQ(degenerate.State,
              Consolidation::Status::NormalEstimationFailed);
    EXPECT_TRUE(degenerate.Positions.empty());
    EXPECT_TRUE(degenerate.Normals.empty());
}

TEST(PointCloudConsolidation, LopRepulsionImprovesMinimumSpacing)
{
    std::vector<glm::vec3> points{};
    for (int y = 0; y < 5; ++y)
    {
        for (int x = 0; x < 5; ++x)
        {
            const float compressedX = 0.035f * x * x;
            points.emplace_back(compressedX, 0.15f * y, 0.0f);
        }
    }

    auto withoutParams = ReferenceParams();
    withoutParams.Method = Consolidation::LopStrategy{};
    withoutParams.TargetPointCount = 16u;
    withoutParams.RepulsionWeight = 0.0;
    withoutParams.ConvergenceTolerance = 5.0e-4;
    const auto without = Consolidation::Consolidate(points, withoutParams);
    ASSERT_FALSE(without.Positions.empty());

    auto withParams = withoutParams;
    withParams.RepulsionWeight = 0.45;
    const auto with = Consolidation::Consolidate(points, withParams);
    ASSERT_FALSE(with.Positions.empty());
    EXPECT_GT(MinimumPairwiseDistance(with.Positions),
              MinimumPairwiseDistance(without.Positions) * 1.05);
}

TEST(PointCloudConsolidation, LopOneStepMatchesPaperEquationOracle)
{
    const std::vector<glm::vec3> points{
        {-0.25f, 0.0f, 0.0f},
        {0.45f, 0.0f, 0.0f},
    };
    constexpr double h = 1.0;
    constexpr double distanceFloor = 0.01 * h;
    const auto theta = [](const double distance)
    {
        return std::exp(-16.0 * distance * distance / (h * h));
    };
    const auto expectedOneStep = [&](const std::size_t seedIndex)
    {
        double l2Weight = 0.0;
        double l2Numerator = 0.0;
        for (const glm::vec3 point : points)
        {
            const double distance = std::abs(
                static_cast<double>(points[seedIndex].x) - point.x);
            const double weight = theta(distance);
            l2Weight += weight;
            l2Numerator += static_cast<double>(point.x) * weight;
        }
        // The implementation deliberately stores each finite L2 iterate in
        // its public float coordinate type before evaluating the L1 step.
        const float initialized = static_cast<float>(l2Numerator / l2Weight);

        double l1Weight = 0.0;
        double l1Numerator = 0.0;
        for (const glm::vec3 point : points)
        {
            const double distance = std::abs(
                static_cast<double>(initialized) - point.x);
            const double weight = theta(distance) /
                std::max(distance, distanceFloor);
            l1Weight += weight;
            l1Numerator += static_cast<double>(point.x) * weight;
        }
        return l1Numerator / l1Weight;
    };

    auto params = ReferenceParams();
    params.Method = Consolidation::LopStrategy{};
    params.SupportRadius = h;
    params.RepulsionWeight = 0.0;
    params.MaxIterations = 1u;
    params.ConvergenceTolerance = 1.0;
    params.TargetPointCount = points.size();
    const auto result = Consolidation::Consolidate(points, params);

    ASSERT_TRUE(result.Succeeded())
        << Consolidation::DebugName(result.State);
    ASSERT_EQ(result.Positions.size(), points.size());
    EXPECT_NEAR(result.Positions[0].x, expectedOneStep(0u), 1.0e-6);
    EXPECT_NEAR(result.Positions[1].x, expectedOneStep(1u), 1.0e-6);
    EXPECT_FLOAT_EQ(result.Positions[0].y, 0.0f);
    EXPECT_FLOAT_EQ(result.Positions[1].z, 0.0f);
}

TEST(PointCloudConsolidation, WlopCorrectsNonUniformDensityMoreThanLop)
{
    const std::vector<glm::vec3> points{
        {0.00f, 0.0f, 0.0f}, {0.02f, 0.0f, 0.0f},
        {0.04f, 0.0f, 0.0f}, {0.06f, 0.0f, 0.0f},
        {0.08f, 0.0f, 0.0f}, {0.30f, 0.0f, 0.0f},
        {0.55f, 0.0f, 0.0f}, {0.80f, 0.0f, 0.0f},
        {1.00f, 0.0f, 0.0f},
    };
    auto lopParams = ReferenceParams();
    lopParams.Method = Consolidation::LopStrategy{};
    lopParams.SupportRadius = 1.1;
    lopParams.TargetPointCount = 6u;
    lopParams.RepulsionWeight = 0.35;
    const auto lop = Consolidation::Consolidate(points, lopParams);
    ASSERT_FALSE(lop.Positions.empty());

    auto wlopParams = lopParams;
    wlopParams.Method = Consolidation::WlopStrategy{};
    const auto wlop = Consolidation::Consolidate(points, wlopParams);
    ASSERT_FALSE(wlop.Positions.empty());
    EXPECT_GT(MinimumPairwiseDistance(wlop.Positions),
              MinimumPairwiseDistance(lop.Positions));
    EXPECT_TRUE(wlop.Diagnostics.UsedDensityWeighting);
    EXPECT_GT(wlop.Diagnostics.DensityContributionCount, 0u);
}

TEST(PointCloudConsolidation, InSupportOutliersHaveBoundedInfluenceOnPlanePatch)
{
    const std::vector<glm::vec3> plane = NoisyPlane(4);
    std::vector<glm::vec3> withOutliers = plane;
    withOutliers.emplace_back(0.0f, 0.0f, 0.45f);
    withOutliers.emplace_back(0.1f, 0.0f, 0.50f);

    auto params = ReferenceParams();
    params.MaxIterations = 12u;
    params.ConvergenceTolerance = 0.0;
    const auto baseline = Consolidation::Consolidate(plane, params);
    const auto outlierRun = Consolidation::Consolidate(withOutliers, params);
    ASSERT_FALSE(baseline.Positions.empty());
    ASSERT_GE(outlierRun.Positions.size(), plane.size());
    double maxDisplacement = 0.0;
    for (std::size_t i = 0u; i < plane.size(); ++i)
    {
        maxDisplacement = std::max(
            maxDisplacement,
            static_cast<double>(glm::distance(
                outlierRun.Positions[i], baseline.Positions[i])));
    }
    // Both outliers are inside h of the patch center. Density weighting need
    // not erase their influence, but must keep the original patch stable to
    // less than one quarter of the 0.65 world-unit support radius.
    EXPECT_LT(maxDisplacement, params.SupportRadius * 0.25);
}

TEST(PointCloudConsolidation, ClopOneStepMatchesGaussianProductEquation)
{
    const auto points = NoisyPlane(3);
    auto params = ClopParams(3u);
    params.SupportRadius = 0.75;
    params.RepulsionWeight = 0.0;
    params.MaxIterations = 1u;
    params.ConvergenceTolerance = 1.0;
    params.TargetPointCount = points.size();

    const auto* strategy =
        std::get_if<Consolidation::ClopStrategy>(&params.Method);
    ASSERT_NE(strategy, nullptr);
    GMM::FitParams fitParams{};
    fitParams.MaxIterations = strategy->MixtureMaxIterations;
    fitParams.RelativeTolerance = strategy->MixtureRelativeTolerance;
    fitParams.CovarianceFloor = strategy->CovarianceFloor;
    fitParams.Seed = params.Seed;
    const auto fit = GMM::FitEM(
        points, strategy->MixtureComponentCount, fitParams);
    ASSERT_TRUE(fit.Succeeded());
    ASSERT_TRUE(fit.Diagnostics.Converged);

    constexpr std::array<AnalyticTerm, 1u> l2Terms{{
        {1.0, 0.1767766952966369},
    }};
    constexpr std::array<AnalyticTerm, 3u> l1Terms{{
        {11.453, 0.11772},
        {29.886, 0.03287},
        {97.761, 0.01010},
    }};
    std::vector<glm::vec3> expected{};
    expected.reserve(points.size());
    for (const glm::vec3 point : points)
    {
        const glm::vec3 initialized = AnalyticClopAttraction(
            fit.Mixture, point, params.SupportRadius, l2Terms);
        expected.push_back(AnalyticClopAttraction(
            fit.Mixture, initialized, params.SupportRadius, l1Terms));
    }

    const auto result = Consolidation::Consolidate(points, params);
    ASSERT_TRUE(result.Succeeded())
        << Consolidation::DebugName(result.State);
    ASSERT_EQ(result.Positions.size(), expected.size());
    for (std::size_t i = 0u; i < expected.size(); ++i)
    {
        EXPECT_NEAR(result.Positions[i].x, expected[i].x, 2.0e-6);
        EXPECT_NEAR(result.Positions[i].y, expected[i].y, 2.0e-6);
        EXPECT_NEAR(result.Positions[i].z, expected[i].z, 2.0e-6);
    }
    EXPECT_EQ(result.Diagnostics.Strategy,
              Consolidation::StrategyKind::Clop);
    EXPECT_TRUE(result.Diagnostics.UsedContinuousAttraction);
    EXPECT_EQ(result.Diagnostics.MixtureComponentCount, 3u);
    EXPECT_TRUE(result.Diagnostics.MixtureConverged);
}

TEST(PointCloudConsolidation, ClopDenoisesPlaneAndSphere)
{
    const auto plane = NoisyPlane();
    auto planeParams = ClopParams(10u);
    planeParams.RepulsionWeight = 0.0;
    planeParams.MaxIterations = 1u;
    planeParams.ConvergenceTolerance = 1.0;
    planeParams.TargetPointCount = 25u;
    const auto projectedPlane =
        Consolidation::Consolidate(plane, planeParams);
    ASSERT_TRUE(projectedPlane.Succeeded())
        << Consolidation::DebugName(projectedPlane.State);
    EXPECT_LT(MeanPlaneError(projectedPlane.Positions),
              MeanPlaneError(plane));
    EXPECT_LT(MeanPlaneError(projectedPlane.Positions), 0.04);

    const auto sphere = NoisySphere();
    auto sphereParams = ClopParams(40u);
    std::get<Consolidation::ClopStrategy>(sphereParams.Method)
        .CovarianceFloor = 1.0e-6;
    sphereParams.SupportRadius = 0.6;
    sphereParams.RepulsionWeight = 0.0;
    sphereParams.MaxIterations = 1u;
    sphereParams.ConvergenceTolerance = 1.0;
    sphereParams.TargetPointCount = 36u;
    const auto projectedSphere =
        Consolidation::Consolidate(sphere, sphereParams);
    ASSERT_TRUE(projectedSphere.Succeeded())
        << Consolidation::DebugName(projectedSphere.State);
    EXPECT_LT(MeanSphereError(projectedSphere.Positions),
              MeanSphereError(sphere));
    // Curved fixtures require a richer mixture than the planar case to avoid
    // covariance-induced shrinkage. This 40-component reference remains
    // below the raw fixture's 0.02081 mean radial error after one step.
    EXPECT_LT(MeanSphereError(projectedSphere.Positions), 0.020);
}

TEST(PointCloudConsolidation, ClopTracksMixtureWorkAndAgreesWithWlop)
{
    const auto points = NoisyPlane(6);
    auto wlopParams = ReferenceParams();
    wlopParams.RepulsionWeight = 0.0;
    wlopParams.MaxIterations = 1u;
    wlopParams.ConvergenceTolerance = 1.0;
    wlopParams.TargetPointCount = 20u;
    const auto wlop = Consolidation::Consolidate(points, wlopParams);
    ASSERT_TRUE(wlop.Succeeded());

    auto richParams = ClopParams(18u);
    richParams.RepulsionWeight = 0.0;
    richParams.MaxIterations = 1u;
    richParams.ConvergenceTolerance = 1.0;
    richParams.TargetPointCount = 20u;
    const auto rich = Consolidation::Consolidate(points, richParams);
    ASSERT_TRUE(rich.Succeeded())
        << Consolidation::DebugName(rich.State);
    ASSERT_EQ(rich.Positions.size(), wlop.Positions.size());
    double meanParityDistance = 0.0;
    for (std::size_t i = 0u; i < rich.Positions.size(); ++i)
    {
        meanParityDistance +=
            static_cast<double>(glm::distance(
                rich.Positions[i], wlop.Positions[i]));
    }
    meanParityDistance /= static_cast<double>(rich.Positions.size());
    EXPECT_LT(meanParityDistance, 0.18);

    auto compactParams = richParams;
    compactParams.Method = Consolidation::ClopStrategy{
        .MixtureComponentCount = 6u,
        .MixtureMaxIterations = 100u,
        .MixtureRelativeTolerance = 1.0e-6,
        .CovarianceFloor = 1.0e-5,
    };
    const auto compact = Consolidation::Consolidate(points, compactParams);
    ASSERT_TRUE(compact.Succeeded())
        << Consolidation::DebugName(compact.State);
    EXPECT_LT(compact.Diagnostics.AttractionContributionCount,
              rich.Diagnostics.AttractionContributionCount);
    EXPECT_EQ(compact.Diagnostics.MixtureComponentCount, 6u);
    EXPECT_EQ(rich.Diagnostics.MixtureComponentCount, 18u);
    EXPECT_LT(MeanPlaneError(compact.Positions), 0.05);
}

TEST(PointCloudConsolidation, ClopKeepsSparseOutlierInfluenceBounded)
{
    const std::vector<glm::vec3> plane = NoisyPlane(4);
    std::vector<glm::vec3> withOutliers = plane;
    withOutliers.emplace_back(0.0f, 0.0f, 0.45f);
    withOutliers.emplace_back(0.1f, 0.0f, 0.50f);

    auto params = ClopParams(8u);
    params.MaxIterations = 1u;
    params.ConvergenceTolerance = 1.0;
    params.RepulsionWeight = 0.0;
    const auto baseline = Consolidation::Consolidate(plane, params);
    const auto outlierRun = Consolidation::Consolidate(withOutliers, params);
    ASSERT_FALSE(baseline.Positions.empty())
        << Consolidation::DebugName(baseline.State);
    ASSERT_GE(outlierRun.Positions.size(), plane.size())
        << Consolidation::DebugName(outlierRun.State);
    double maxDisplacement = 0.0;
    for (std::size_t i = 0u; i < plane.size(); ++i)
    {
        maxDisplacement = std::max(
            maxDisplacement,
            static_cast<double>(glm::distance(
                outlierRun.Positions[i], baseline.Positions[i])));
    }
    EXPECT_LT(maxDisplacement, params.SupportRadius * 0.25);
}

TEST(PointCloudConsolidation, ClopIsDeterministicAndFailsClosed)
{
    const auto points = NoisyPlane(5);
    auto params = ClopParams(8u);
    params.TargetPointCount = 16u;
    params.MaxIterations = 3u;
    params.ConvergenceTolerance = 0.0;
    const auto first = Consolidation::Consolidate(points, params);
    const auto second = Consolidation::Consolidate(points, params);
    ASSERT_EQ(first.State, second.State);
    EXPECT_EQ(first.Positions, second.Positions);
    EXPECT_EQ(first.Diagnostics.MixtureIterations,
              second.Diagnostics.MixtureIterations);

    const auto expectFailure = [&](
        const Consolidation::ClopStrategy strategy,
        const Consolidation::Status status)
    {
        auto invalid = params;
        invalid.Method = strategy;
        const auto result = Consolidation::Consolidate(points, invalid);
        EXPECT_EQ(result.State, status);
        EXPECT_TRUE(result.Positions.empty());
    };
    expectFailure(
        Consolidation::ClopStrategy{.MixtureComponentCount = 0u},
        Consolidation::Status::InvalidMixtureComponentCount);
    expectFailure(
        Consolidation::ClopStrategy{
            .MixtureComponentCount =
                static_cast<std::uint32_t>(points.size() + 1u)},
        Consolidation::Status::InvalidMixtureComponentCount);
    expectFailure(
        Consolidation::ClopStrategy{
            .MixtureComponentCount = 4u,
            .MixtureMaxIterations = 0u},
        Consolidation::Status::InvalidMixtureParameters);
    expectFailure(
        Consolidation::ClopStrategy{
            .MixtureComponentCount = 4u,
            .CovarianceFloor = 0.0},
        Consolidation::Status::InvalidMixtureParameters);
}

TEST(PointCloudConsolidation, StrategyAndSeedAreBitwiseDeterministic)
{
    const auto points = NoisyPlane();
    auto params = ReferenceParams();
    params.TargetPointCount = 24u;
    const auto first = Consolidation::Consolidate(points, params);
    const auto second = Consolidation::Consolidate(points, params);
    ASSERT_EQ(first.State, second.State);
    EXPECT_EQ(first.Positions, second.Positions);
    EXPECT_EQ(first.Diagnostics.Strategy,
              Consolidation::StrategyKind::Wlop);
    EXPECT_EQ(first.Diagnostics.Implementation,
              Consolidation::kCpuReferenceImplementation);

    params.Method = Consolidation::LopStrategy{};
    const auto lop = Consolidation::Consolidate(points, params);
    EXPECT_EQ(lop.Diagnostics.Strategy,
              Consolidation::StrategyKind::Lop);
    EXPECT_FALSE(lop.Diagnostics.UsedDensityWeighting);
    ASSERT_EQ(lop.Positions.size(), 24u);
    EXPECT_NEAR(MeanPlaneError(lop.Positions),
                0.032516757084522396, 1.0e-8);
}

TEST(PointCloudConsolidation, ConcurrentCallersAreBitwiseDeterministic)
{
    const auto points = NoisyPlane(6);
    auto params = ReferenceParams();
    params.TargetPointCount = 20u;
    params.MaxIterations = 16u;
    params.ConvergenceTolerance = 0.0;

    auto firstFuture = std::async(std::launch::async, [&]
    {
        return Consolidation::Consolidate(points, params);
    });
    auto secondFuture = std::async(std::launch::async, [&]
    {
        return Consolidation::Consolidate(points, params);
    });
    const auto serial = Consolidation::Consolidate(points, params);
    const auto first = firstFuture.get();
    const auto second = secondFuture.get();

    ASSERT_EQ(first.State, serial.State);
    ASSERT_EQ(second.State, serial.State);
    EXPECT_EQ(first.Positions, serial.Positions);
    EXPECT_EQ(second.Positions, serial.Positions);
    EXPECT_EQ(first.Diagnostics.Iterations, serial.Diagnostics.Iterations);
    EXPECT_EQ(second.Diagnostics.Iterations, serial.Diagnostics.Iterations);
}

TEST(PointCloudConsolidation, ClopConcurrentCallersAreBitwiseDeterministic)
{
    const auto points = NoisyPlane(6);
    auto params = ClopParams(10u);
    params.TargetPointCount = 20u;
    params.MaxIterations = 4u;
    params.ConvergenceTolerance = 0.0;

    auto firstFuture = std::async(std::launch::async, [&]
    {
        return Consolidation::Consolidate(points, params);
    });
    auto secondFuture = std::async(std::launch::async, [&]
    {
        return Consolidation::Consolidate(points, params);
    });
    const auto serial = Consolidation::Consolidate(points, params);
    const auto first = firstFuture.get();
    const auto second = secondFuture.get();

    ASSERT_EQ(first.State, serial.State);
    ASSERT_EQ(second.State, serial.State);
    EXPECT_EQ(first.Positions, serial.Positions);
    EXPECT_EQ(second.Positions, serial.Positions);
    EXPECT_EQ(first.Diagnostics.MixtureIterations,
              serial.Diagnostics.MixtureIterations);
    EXPECT_EQ(second.Diagnostics.MixtureIterations,
              serial.Diagnostics.MixtureIterations);
}

TEST(PointCloudConsolidation, InvalidRequestsFailWithoutPublishingPositions)
{
    const std::vector<glm::vec3> points{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
    };
    const auto expectFailure = [](
        const Consolidation::Result& result,
        const Consolidation::Status status)
    {
        EXPECT_EQ(result.State, status);
        EXPECT_TRUE(result.Positions.empty());
    };

    expectFailure(Consolidation::Consolidate(
                      std::span<const glm::vec3>{}, ReferenceParams()),
                  Consolidation::Status::EmptyInput);
    expectFailure(Consolidation::Consolidate(
                      std::span<const glm::vec3>{points.data(), 1u},
                      ReferenceParams()),
                  Consolidation::Status::TooFewPoints);

    auto params = ReferenceParams();
    params.SupportRadius = 0.0;
    expectFailure(Consolidation::Consolidate(points, params),
                  Consolidation::Status::InvalidSupportRadius);
    params = ReferenceParams();
    params.RepulsionWeight = 0.5;
    expectFailure(Consolidation::Consolidate(points, params),
                  Consolidation::Status::InvalidRepulsionWeight);
    params = ReferenceParams();
    params.MaxIterations = 0u;
    expectFailure(Consolidation::Consolidate(points, params),
                  Consolidation::Status::InvalidIterationLimit);
    params = ReferenceParams();
    params.ConvergenceTolerance = -1.0;
    expectFailure(Consolidation::Consolidate(points, params),
                  Consolidation::Status::InvalidConvergenceTolerance);
    params = ReferenceParams();
    params.TargetPointCount = 3u;
    expectFailure(Consolidation::Consolidate(points, params),
                  Consolidation::Status::InvalidTargetCount);
    params = ReferenceParams();
    params.MaxInputPointCount = 1u;
    expectFailure(Consolidation::Consolidate(points, params),
                  Consolidation::Status::ResourceLimit);

    auto nonFinite = points;
    nonFinite[1].z = std::numeric_limits<float>::quiet_NaN();
    expectFailure(Consolidation::Consolidate(nonFinite, ReferenceParams()),
                  Consolidation::Status::NonFiniteInput);

    params = ReferenceParams();
    params.SupportRadius = 0.25;
    expectFailure(Consolidation::Consolidate(points, params),
                  Consolidation::Status::EmptyNeighborhood);
}

TEST(PointCloudConsolidation, DegenerateAndNonConvergedStatesStayFinite)
{
    const std::vector<glm::vec3> coincident(6u, glm::vec3(1.0f));
    auto params = ReferenceParams();
    const auto stable = Consolidation::Consolidate(coincident, params);
    ASSERT_TRUE(stable.Succeeded())
        << Consolidation::DebugName(stable.State);
    ExpectFiniteOutput(stable);

    params = ReferenceParams();
    params.MaxIterations = 1u;
    params.ConvergenceTolerance = 0.0;
    const auto unfinished =
        Consolidation::Consolidate(NoisyPlane(), params);
    EXPECT_EQ(unfinished.State, Consolidation::Status::NotConverged);
    EXPECT_FALSE(unfinished.Diagnostics.Converged);
    EXPECT_EQ(unfinished.Diagnostics.Iterations, 1u);
    ExpectFiniteOutput(unfinished);
}

TEST(PointCloudConsolidation, CloudOverloadRejectsDeletedSlots)
{
    Geometry::PointCloud::Cloud cloud{};
    const auto first = cloud.AddPoint({0.0f, 0.0f, 0.0f});
    static_cast<void>(cloud.AddPoint({0.1f, 0.0f, 0.0f}));
    cloud.DeletePoint(first);
    const auto result =
        Consolidation::Consolidate(cloud, ReferenceParams());
    EXPECT_EQ(result.State, Consolidation::Status::InvalidCloud);
    EXPECT_TRUE(result.Positions.empty());
}
