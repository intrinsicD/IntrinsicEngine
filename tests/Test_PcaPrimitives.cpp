#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

import Geometry;

namespace
{
    [[nodiscard]] glm::quat MakeRotation()
    {
        return glm::normalize(
            glm::angleAxis(0.7f, glm::normalize(glm::vec3{1.0f, 2.0f, -0.5f}))
          * glm::angleAxis(-0.35f, glm::normalize(glm::vec3{-1.0f, 0.25f, 0.75f})));
    }

    [[nodiscard]] std::vector<glm::vec3> MakeRotatedBoxCorners(
        const glm::vec3& center,
        const glm::vec3& extents,
        const glm::quat& rotation)
    {
        std::vector<glm::vec3> points;
        points.reserve(8);
        for (int sx : {-1, 1})
        {
            for (int sy : {-1, 1})
            {
                for (int sz : {-1, 1})
                {
                    const glm::vec3 local{
                        extents.x * static_cast<float>(sx),
                        extents.y * static_cast<float>(sy),
                        extents.z * static_cast<float>(sz),
                    };
                    points.push_back(center + rotation * local);
                }
            }
        }
        return points;
    }
}

TEST(PcaPrimitives, PcaRecoversDominantAxisForLineSamples)
{
    const glm::vec3 direction = glm::normalize(glm::vec3{2.0f, -1.0f, 0.5f});
    const glm::vec3 center{1.5f, -0.5f, 2.0f};
    std::vector<glm::vec3> points = {
        center - 3.0f * direction,
        center - 1.0f * direction,
        center + 0.5f * direction,
        center + 4.0f * direction,
        {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
    };

    const Geometry::PCAResult pca = Geometry::ToPCA(points);
    ASSERT_TRUE(pca.Valid);
    EXPECT_GT(pca.Eigenvalues.x, pca.Eigenvalues.y);
    EXPECT_GT(pca.Eigenvalues.y + 1.0e-6f, pca.Eigenvalues.z);
    EXPECT_NEAR(std::abs(glm::dot(glm::normalize(pca.Eigenvectors[0]), direction)), 1.0f, 1.0e-4f);
    EXPECT_NEAR(glm::distance(pca.Mean, center + 0.125f * direction), 0.0f, 1.0e-5f);
}

TEST(PcaPrimitives, PlaneFitRecoversTiltedPlane)
{
    const glm::vec3 expectedNormal = glm::normalize(glm::vec3{2.0f, -3.0f, 1.0f});
    std::vector<glm::vec3> points;
    for (float x : {-2.0f, -0.5f, 1.0f, 3.0f})
    {
        for (float y : {-1.0f, 0.5f, 2.0f})
        {
            const float z = 4.0f - 2.0f * x + 3.0f * y;
            points.emplace_back(x, y, z);
        }
    }

    const Geometry::Plane plane = Geometry::ToPlane(points);
    EXPECT_NEAR(std::abs(glm::dot(plane.Normal, expectedNormal)), 1.0f, 1.0e-4f);
    for (const glm::vec3& point : points)
    {
        EXPECT_NEAR(Geometry::SignedDistance(plane, point), 0.0, 1.0e-4);
    }
}

TEST(PcaPrimitives, SegmentFitTracksPrincipalExtent)
{
    const glm::vec3 direction = glm::normalize(glm::vec3{-1.0f, 2.0f, 0.5f});
    const glm::vec3 origin{2.0f, -1.0f, 0.5f};
    const std::array<float, 4> parameters{-2.0f, -0.25f, 1.5f, 4.0f};

    std::vector<glm::vec3> points;
    points.reserve(parameters.size());
    for (float t : parameters)
    {
        points.push_back(origin + direction * t);
    }

    const Geometry::Segment segment = Geometry::ToSegment(points);
    EXPECT_NEAR(segment.GetLength(), 6.0f, 1.0e-4f);
    EXPECT_NEAR(std::abs(glm::dot(glm::normalize(segment.GetDirection()), direction)), 1.0f, 1.0e-4f);
    EXPECT_NEAR(glm::distance(Geometry::ClosestPoint(segment, origin - 2.0f * direction), origin - 2.0f * direction), 0.0f, 1.0e-4f);
    EXPECT_NEAR(glm::distance(Geometry::ClosestPoint(segment, origin + 4.0f * direction), origin + 4.0f * direction), 0.0f, 1.0e-4f);
}

TEST(PcaPrimitives, OBBFitContainsRotatedBoxCorners)
{
    const glm::vec3 center{1.0f, -2.0f, 0.5f};
    const glm::vec3 extents{4.0f, 2.0f, 1.0f};
    const glm::quat rotation = MakeRotation();
    const std::vector<glm::vec3> points = MakeRotatedBoxCorners(center, extents, rotation);

    const Geometry::OBB obb = Geometry::ToOOBB(points);
    for (const glm::vec3& point : points)
    {
        const glm::vec3 local = obb.ToLocal(point);
        EXPECT_LE(std::abs(local.x), obb.Extents.x + 1.0e-3f);
        EXPECT_LE(std::abs(local.y), obb.Extents.y + 1.0e-3f);
        EXPECT_LE(std::abs(local.z), obb.Extents.z + 1.0e-3f);
    }

    std::array<float, 3> sortedExpected{extents.x, extents.y, extents.z};
    std::array<float, 3> sortedActual{obb.Extents.x, obb.Extents.y, obb.Extents.z};
    std::sort(sortedExpected.begin(), sortedExpected.end());
    std::sort(sortedActual.begin(), sortedActual.end());
    EXPECT_NEAR(sortedActual[0], sortedExpected[0], 1.0e-3f);
    EXPECT_NEAR(sortedActual[1], sortedExpected[1], 1.0e-3f);
    EXPECT_NEAR(sortedActual[2], sortedExpected[2], 1.0e-3f);
}

TEST(PcaPrimitives, EllipsoidFitContainsRotatedBoxCorners)
{
    const glm::vec3 center{-1.0f, 3.0f, 2.5f};
    const glm::vec3 extents{3.0f, 1.5f, 0.75f};
    const glm::quat rotation = MakeRotation();
    const std::vector<glm::vec3> points = MakeRotatedBoxCorners(center, extents, rotation);

    const Geometry::Ellipsoid ellipsoid = Geometry::ToEllipsoiod(points);
    for (const glm::vec3& point : points)
    {
        EXPECT_TRUE(Geometry::Contains(ellipsoid, point));
    }
}
