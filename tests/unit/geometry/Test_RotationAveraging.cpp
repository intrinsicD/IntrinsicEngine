#include <gtest/gtest.h>

#include <vector>
#include <glm/glm.hpp>

import Geometry.Rotation;
import Geometry.RotationAveraging;

namespace
{
    using namespace Geometry::Rotation;
}

TEST(GeometryRotationAveraging, MeansOfEqualRotationsReturnIt)
{
    const glm::mat3 r = RandomRotation(5);
    const std::vector<glm::mat3> rots(6, r);

    EXPECT_LT(ChordalDistance(ChordalMean(rots), r), 1e-3f);
    EXPECT_LT(ChordalDistance(QuaternionMean(rots), r), 1e-3f);
    const auto karcher = KarcherMean(rots);
    ASSERT_TRUE(karcher.Valid);
    EXPECT_LT(ChordalDistance(karcher.Rotation, r), 1e-3f);
    const auto median = GeodesicMedian(rots);
    ASSERT_TRUE(median.Valid);
    EXPECT_LT(ChordalDistance(median.Rotation, r), 1e-3f);
}

TEST(GeometryRotationAveraging, TwoRotationMeanIsMidpoint)
{
    const glm::vec3 axis = glm::normalize(glm::vec3{0.2f, 1.0f, -0.3f});
    const glm::mat3 a = Exp(axis * 0.0f);   // identity
    const glm::mat3 b = Exp(axis * 1.0f);   // 1 rad about axis
    const glm::mat3 expectedMid = Exp(axis * 0.5f);
    const std::vector<glm::mat3> rots{a, b};

    // Chordal, Karcher, and quaternion means all give the geodesic midpoint here.
    EXPECT_LT(AngularDistance(ChordalMean(rots), expectedMid), 1e-3f);
    EXPECT_LT(AngularDistance(QuaternionMean(rots), expectedMid), 1e-3f);
    const auto karcher = KarcherMean(rots);
    ASSERT_TRUE(karcher.Valid);
    EXPECT_TRUE(karcher.Converged);
    EXPECT_LT(AngularDistance(karcher.Rotation, expectedMid), 1e-3f);
}

TEST(GeometryRotationAveraging, WeightedMeanLeansToHeavyRotation)
{
    const glm::mat3 a = RandomRotation(1);
    const glm::mat3 b = RandomRotation(2);
    const std::vector<glm::mat3> rots{a, b};
    const std::vector<float> weights{100.0f, 1.0f};
    const glm::mat3 mean = ChordalMean(rots, weights);
    EXPECT_LT(AngularDistance(mean, a), AngularDistance(mean, b));
}

TEST(GeometryRotationAveraging, MedianIsRobustToOutliers)
{
    // A tight cluster near identity plus gross outliers.
    const glm::vec3 axis = glm::normalize(glm::vec3{1.0f, 0.0f, 0.0f});
    std::vector<glm::mat3> rots;
    for (float t : {-0.02f, -0.01f, 0.0f, 0.01f, 0.02f})
        rots.push_back(Exp(axis * t));
    rots.push_back(Exp(axis * 2.5f)); // outlier
    rots.push_back(Exp(axis * 2.6f)); // outlier

    const glm::mat3 mean = KarcherMean(rots).Rotation;
    const glm::mat3 median = GeodesicMedian(rots).Rotation;
    const glm::mat3 cluster = glm::mat3(1.0f); // cluster centroid ~ identity

    // The L1 median sits much closer to the cluster than the L2 mean.
    EXPECT_LT(AngularDistance(median, cluster), AngularDistance(mean, cluster));
    EXPECT_LT(AngularDistance(median, cluster), 0.2f);
}

TEST(GeometryRotationAveraging, Deterministic)
{
    std::vector<glm::mat3> rots;
    for (std::uint64_t s = 1; s <= 5; ++s) rots.push_back(RandomRotation(s));
    EXPECT_LT(ChordalDistance(ChordalMean(rots), ChordalMean(rots)), 1e-6f);
    EXPECT_LT(ChordalDistance(KarcherMean(rots).Rotation, KarcherMean(rots).Rotation), 1e-6f);
}

TEST(GeometryRotationAveraging, FailClosedOnEmpty)
{
    const std::vector<glm::mat3> empty{};
    EXPECT_LT(ChordalDistance(ChordalMean(empty), glm::mat3(1.0f)), 1e-6f);
    EXPECT_LT(ChordalDistance(QuaternionMean(empty), glm::mat3(1.0f)), 1e-6f);
    EXPECT_FALSE(KarcherMean(empty).Valid);
    EXPECT_FALSE(GeodesicMedian(empty).Valid);
}
