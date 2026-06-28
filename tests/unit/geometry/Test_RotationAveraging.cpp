#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>

import Geometry.Rotation;
import Geometry.RotationAveraging;

namespace
{
    using namespace Geometry::Rotation;

    [[nodiscard]] RotationAverageOptions WithWeights(const std::vector<float>& weights)
    {
        RotationAverageOptions options{};
        options.Weights = std::span<const float>(weights.data(), weights.size());
        return options;
    }

    void ExpectFinite(const RotationAverageResult& result)
    {
        for (int c = 0; c < 3; ++c)
        {
            for (int r = 0; r < 3; ++r)
            {
                EXPECT_TRUE(std::isfinite(result.Rotation[c][r]));
            }
        }
        EXPECT_TRUE(std::isfinite(result.ResidualRadians));
    }

    void ExpectNearRotation(const RotationAverageResult& result,
                            const glm::mat3& expected,
                            float tolerance = 1e-3f)
    {
        ExpectFinite(result);
        ASSERT_TRUE(result.Valid);
        EXPECT_TRUE(result.Succeeded());
        EXPECT_LT(AngularDistance(result.Rotation, expected), tolerance);
    }

    void ExpectMatrixBitEqual(const glm::mat3& a, const glm::mat3& b)
    {
        EXPECT_EQ(std::memcmp(&a[0][0], &b[0][0], sizeof(glm::mat3)), 0);
    }
}

TEST(GeometryRotationAveraging, AllRoutinesReturnRepeatedRotation)
{
    const glm::mat3 rotation = RandomRotation(5);
    const std::vector<glm::mat3> rotations(6, rotation);

    ExpectNearRotation(ChordalMean(rotations), rotation);
    ExpectNearRotation(QuaternionMean(rotations), rotation);
    ExpectNearRotation(KarcherMean(rotations), rotation);
    ExpectNearRotation(GeodesicMedian(rotations), rotation);
    ExpectNearRotation(QuaternionMedian(rotations), rotation);
}

TEST(GeometryRotationAveraging, ChordalAndKarcherAgreeOnCluster)
{
    const glm::vec3 axis = glm::normalize(glm::vec3{0.2f, 1.0f, -0.3f});
    const glm::mat3 base = RandomRotation(13);
    std::vector<glm::mat3> rotations;
    for (float angle : {-0.03f, -0.01f, 0.0f, 0.015f, 0.025f})
    {
        rotations.push_back(base * Exp(axis * angle));
    }

    const RotationAverageResult chordal = ChordalMean(rotations);
    const RotationAverageResult karcher = KarcherMean(rotations);

    ASSERT_TRUE(chordal.Valid);
    ASSERT_TRUE(karcher.Valid);
    EXPECT_TRUE(chordal.Converged);
    EXPECT_TRUE(karcher.Converged);
    EXPECT_LT(AngularDistance(chordal.Rotation, karcher.Rotation), 5e-4f);
}

TEST(GeometryRotationAveraging, WeightedKarcherMeanMatchesAnalyticSlerp)
{
    const glm::vec3 axis = glm::normalize(glm::vec3{0.2f, 1.0f, -0.3f});
    const glm::mat3 a = Exp(axis * 0.0f);
    const glm::mat3 b = Exp(axis * 1.0f);
    const std::vector<glm::mat3> rotations{a, b};
    const std::vector<float> weights{1.0f, 3.0f};
    const glm::mat3 expected = Exp(axis * 0.75f);

    const RotationAverageResult mean = KarcherMean(rotations, WithWeights(weights));

    ExpectNearRotation(mean, expected, 1e-4f);
    EXPECT_TRUE(mean.Converged);
}

TEST(GeometryRotationAveraging, MediansStayCloserToInliersThanMeansWithOutliers)
{
    const glm::vec3 axis = glm::normalize(glm::vec3{1.0f, 0.1f, 0.0f});
    std::vector<glm::mat3> rotations;
    for (float angle : {-0.02f, -0.01f, 0.0f, 0.01f, 0.02f})
    {
        rotations.push_back(Exp(axis * angle));
    }
    rotations.push_back(Exp(axis * 2.45f));
    rotations.push_back(Exp(axis * 2.60f));

    const glm::mat3 cluster = glm::mat3(1.0f);
    const RotationAverageResult chordal = ChordalMean(rotations);
    const RotationAverageResult quaternion = QuaternionMean(rotations);
    const RotationAverageResult geodesic = KarcherMean(rotations);
    const RotationAverageResult geodesicMedian = GeodesicMedian(rotations);
    const RotationAverageResult quaternionMedian = QuaternionMedian(rotations);

    ASSERT_TRUE(chordal.Valid);
    ASSERT_TRUE(quaternion.Valid);
    ASSERT_TRUE(geodesic.Valid);
    ASSERT_TRUE(geodesicMedian.Valid);
    ASSERT_TRUE(quaternionMedian.Valid);

    EXPECT_LT(AngularDistance(geodesicMedian.Rotation, cluster),
              AngularDistance(geodesic.Rotation, cluster));
    EXPECT_LT(AngularDistance(quaternionMedian.Rotation, cluster),
              AngularDistance(quaternion.Rotation, cluster));
    EXPECT_LT(AngularDistance(geodesicMedian.Rotation, cluster),
              AngularDistance(chordal.Rotation, cluster));
    EXPECT_LT(AngularDistance(geodesicMedian.Rotation, cluster), 0.2f);
    EXPECT_LT(AngularDistance(quaternionMedian.Rotation, cluster), 0.2f);
}

TEST(GeometryRotationAveraging, DeterministicAcrossRepeatedCallsAndPermutation)
{
    std::vector<glm::mat3> rotations;
    for (std::uint64_t seed = 1; seed <= 6; ++seed)
    {
        rotations.push_back(RandomRotation(seed));
    }
    std::vector<glm::mat3> permuted = rotations;
    std::reverse(permuted.begin(), permuted.end());

    const RotationAverageResult chordalA = ChordalMean(rotations);
    const RotationAverageResult chordalB = ChordalMean(rotations);
    const RotationAverageResult chordalPermuted = ChordalMean(permuted);
    const RotationAverageResult karcherA = KarcherMean(rotations);
    const RotationAverageResult karcherB = KarcherMean(rotations);
    const RotationAverageResult karcherPermuted = KarcherMean(permuted);

    ASSERT_TRUE(chordalA.Valid);
    ASSERT_TRUE(karcherA.Valid);
    ExpectMatrixBitEqual(chordalA.Rotation, chordalB.Rotation);
    ExpectMatrixBitEqual(chordalA.Rotation, chordalPermuted.Rotation);
    ExpectMatrixBitEqual(karcherA.Rotation, karcherB.Rotation);
    ExpectMatrixBitEqual(karcherA.Rotation, karcherPermuted.Rotation);
}

TEST(GeometryRotationAveraging, FailClosedStatuses)
{
    const std::vector<glm::mat3> empty{};
    const RotationAverageResult emptyResult = ChordalMean(empty);
    EXPECT_EQ(emptyResult.Status, RotationAverageStatus::EmptyInput);
    EXPECT_FALSE(emptyResult.Valid);
    ExpectFinite(emptyResult);

    const glm::mat3 singleRotation = RandomRotation(11);
    const std::vector<glm::mat3> single{singleRotation};
    const RotationAverageResult singleResult = QuaternionMedian(single);
    EXPECT_EQ(singleResult.Status, RotationAverageStatus::SingleSample);
    EXPECT_TRUE(singleResult.Valid);
    EXPECT_TRUE(singleResult.Converged);
    EXPECT_EQ(singleResult.Iterations, 0);
    ExpectMatrixBitEqual(singleResult.Rotation, singleRotation);

    const std::vector<glm::mat3> two{glm::mat3(1.0f), Exp(glm::vec3{3.14159265f, 0.0f, 0.0f})};
    EXPECT_EQ(ChordalMean(two).Status, RotationAverageStatus::DegenerateInput);
    EXPECT_EQ(QuaternionMean(two).Status, RotationAverageStatus::DegenerateInput);
    EXPECT_EQ(KarcherMean(two).Status, RotationAverageStatus::DegenerateInput);
    EXPECT_EQ(GeodesicMedian(two).Status, RotationAverageStatus::DegenerateInput);
    EXPECT_EQ(QuaternionMedian(two).Status, RotationAverageStatus::DegenerateInput);

    const std::vector<glm::mat3> rotations{glm::mat3(1.0f), Exp(glm::vec3{0.2f, 0.0f, 0.0f})};
    const std::vector<float> mismatchedWeights{1.0f};
    EXPECT_EQ(ChordalMean(rotations, WithWeights(mismatchedWeights)).Status,
              RotationAverageStatus::WeightSizeMismatch);

    const std::vector<float> invalidWeights{1.0f, 0.0f};
    EXPECT_EQ(QuaternionMean(rotations, WithWeights(invalidWeights)).Status,
              RotationAverageStatus::InvalidWeight);

    std::vector<glm::mat3> nonFinite = rotations;
    nonFinite[1][0][0] = std::numeric_limits<float>::infinity();
    const RotationAverageResult nonFiniteResult = KarcherMean(nonFinite);
    EXPECT_EQ(nonFiniteResult.Status, RotationAverageStatus::NonFiniteInput);
    EXPECT_FALSE(nonFiniteResult.Valid);
    ExpectFinite(nonFiniteResult);
}
