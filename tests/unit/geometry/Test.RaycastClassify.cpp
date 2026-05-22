// GEOM-007 Slice 3 — parity tests for `Geometry::RayTriangle_Classify`.
//
// These tests assert that the classifying entry point produces results that
// agree with the existing `Geometry::RayTriangle_Watertight` numerical kernel
// for every hit/miss/boundary case the legacy callers care about, and that
// the new diagnostic fields (`Kind`, `OnRay`, `OnTriangle`) carry the
// classification information existing callers had to reconstruct by hand.

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <optional>

#include <glm/glm.hpp>

import Geometry.Raycast;
import Geometry.Primitives;
import Geometry.IntersectionClassification;

namespace
{
    using Geometry::Ray;
    using Geometry::RayTriangle_Classify;
    using Geometry::RayTriangle_Watertight;
    namespace IX = Geometry::Intersection;

    constexpr glm::vec3 kA{0.0f, 0.0f, 0.0f};
    constexpr glm::vec3 kB{1.0f, 0.0f, 0.0f};
    constexpr glm::vec3 kC{0.0f, 1.0f, 0.0f};

    // Helper: assert that the classifying record numerically matches the
    // watertight record on the geometric fields. Bit-exact on T/U/V because
    // `RayTriangle_Classify` reuses the watertight kernel.
    void ExpectGeometryParity(const IX::RayTriangleResult& classify,
                              const Geometry::RayTriangleHit& watertight)
    {
        EXPECT_DOUBLE_EQ(classify.RayParam, static_cast<double>(watertight.T));
        EXPECT_DOUBLE_EQ(classify.WA, static_cast<double>(watertight.U));
        EXPECT_DOUBLE_EQ(classify.WB, static_cast<double>(watertight.V));
        EXPECT_DOUBLE_EQ(classify.WC, 1.0
                                       - static_cast<double>(watertight.U)
                                       - static_cast<double>(watertight.V));
    }
}

// -----------------------------------------------------------------------------
// Hit/miss parity with the existing watertight kernel.
// -----------------------------------------------------------------------------

TEST(RayTriangleClassifyParity, InteriorHitMatchesWatertightAndReportsProper)
{
    const Ray ray{{0.25f, 0.25f, -1.0f}, {0.0f, 0.0f, 1.0f}};
    const auto watertight = RayTriangle_Watertight(ray, kA, kB, kC);
    ASSERT_TRUE(watertight.has_value());

    const auto classify = RayTriangle_Classify(ray, kA, kB, kC);
    EXPECT_EQ(classify.Kind, IX::Kind::Proper);
    EXPECT_EQ(classify.OnTriangle, IX::TriangleFeature::Interior);
    EXPECT_EQ(classify.OnRay, IX::RayFeature::Interior);
    ExpectGeometryParity(classify, *watertight);
    EXPECT_NEAR(classify.Point.z, 0.0f, 1.0e-6f);
}

TEST(RayTriangleClassifyParity, MissReportsNoneAndMatchesWatertight)
{
    const Ray ray{{2.0f, 2.0f, -1.0f}, {0.0f, 0.0f, 1.0f}};
    const auto watertight = RayTriangle_Watertight(ray, kA, kB, kC);
    ASSERT_FALSE(watertight.has_value());

    const auto classify = RayTriangle_Classify(ray, kA, kB, kC);
    EXPECT_EQ(classify.Kind, IX::Kind::None);
    EXPECT_EQ(classify.OnRay, IX::RayFeature::None);
    EXPECT_EQ(classify.OnTriangle, IX::TriangleFeature::None);
}

TEST(RayTriangleClassifyParity, BehindTMinReportsNone)
{
    const Ray ray{{0.25f, 0.25f, -1.0f}, {0.0f, 0.0f, 1.0f}};
    const auto classify = RayTriangle_Classify(ray, kA, kB, kC, /*tMin=*/1.5f);
    EXPECT_EQ(classify.Kind, IX::Kind::None);
}

TEST(RayTriangleClassifyParity, BeyondTMaxReportsNone)
{
    const Ray ray{{0.25f, 0.25f, -1.0f}, {0.0f, 0.0f, 1.0f}};
    const auto classify = RayTriangle_Classify(ray, kA, kB, kC, /*tMin=*/0.0f, /*tMax=*/0.5f);
    EXPECT_EQ(classify.Kind, IX::Kind::None);
}

// -----------------------------------------------------------------------------
// Degenerate inputs map to Kind::DegenerateInput, not Kind::None, so callers
// can distinguish "asked an unanswerable question" from "answer is empty".
// -----------------------------------------------------------------------------

TEST(RayTriangleClassifyDegenerate, ZeroAreaTriangleReportsDegenerateInput)
{
    const Ray ray{{0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}};
    const glm::vec3 a{0.0f, 0.0f, 0.0f};
    const glm::vec3 b{1.0f, 0.0f, 0.0f};
    const glm::vec3 c{2.0f, 0.0f, 0.0f};

    const auto classify = RayTriangle_Classify(ray, a, b, c);
    EXPECT_EQ(classify.Kind, IX::Kind::DegenerateInput);
}

TEST(RayTriangleClassifyDegenerate, InvalidRayReportsDegenerateInput)
{
    // Zero-length direction is rejected by `Geometry::Validation::IsValid(Ray)`.
    const Ray ray{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    const auto classify = RayTriangle_Classify(ray, kA, kB, kC);
    EXPECT_EQ(classify.Kind, IX::Kind::DegenerateInput);
}

// -----------------------------------------------------------------------------
// Boundary classification — vertex and edge hits surface as `Kind::Touching`
// with the right `OnTriangle` feature.
// -----------------------------------------------------------------------------

TEST(RayTriangleClassifyBoundary, VertexAHitReportsTouchingVertexA)
{
    const Ray ray{{0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}};
    const auto classify = RayTriangle_Classify(ray, kA, kB, kC);
    EXPECT_EQ(classify.Kind, IX::Kind::Touching);
    EXPECT_EQ(classify.OnTriangle, IX::TriangleFeature::VertexA);
    EXPECT_NEAR(classify.WA, 1.0, 1.0e-5);
    EXPECT_NEAR(classify.WB, 0.0, 1.0e-5);
    EXPECT_NEAR(classify.WC, 0.0, 1.0e-5);
}

TEST(RayTriangleClassifyBoundary, VertexBHitReportsTouchingVertexB)
{
    const Ray ray{{1.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}};
    const auto classify = RayTriangle_Classify(ray, kA, kB, kC);
    EXPECT_EQ(classify.Kind, IX::Kind::Touching);
    EXPECT_EQ(classify.OnTriangle, IX::TriangleFeature::VertexB);
}

TEST(RayTriangleClassifyBoundary, VertexCHitReportsTouchingVertexC)
{
    const Ray ray{{0.0f, 1.0f, -1.0f}, {0.0f, 0.0f, 1.0f}};
    const auto classify = RayTriangle_Classify(ray, kA, kB, kC);
    EXPECT_EQ(classify.Kind, IX::Kind::Touching);
    EXPECT_EQ(classify.OnTriangle, IX::TriangleFeature::VertexC);
}

TEST(RayTriangleClassifyBoundary, EdgeABMidpointReportsTouchingEdgeAB)
{
    // Midpoint of edge AB has WC == 0, WA == WB == 0.5.
    const Ray ray{{0.5f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}};
    const auto classify = RayTriangle_Classify(ray, kA, kB, kC);
    EXPECT_EQ(classify.Kind, IX::Kind::Touching);
    EXPECT_EQ(classify.OnTriangle, IX::TriangleFeature::EdgeAB);
    EXPECT_NEAR(classify.WC, 0.0, 1.0e-5);
}

TEST(RayTriangleClassifyBoundary, EdgeBCMidpointReportsTouchingEdgeBC)
{
    // Midpoint of edge BC has WA == 0, WB == WC == 0.5.
    const Ray ray{{0.5f, 0.5f, -1.0f}, {0.0f, 0.0f, 1.0f}};
    const auto classify = RayTriangle_Classify(ray, kA, kB, kC);
    EXPECT_EQ(classify.Kind, IX::Kind::Touching);
    EXPECT_EQ(classify.OnTriangle, IX::TriangleFeature::EdgeBC);
    EXPECT_NEAR(classify.WA, 0.0, 1.0e-5);
}

TEST(RayTriangleClassifyBoundary, EdgeCAMidpointReportsTouchingEdgeCA)
{
    // Midpoint of edge CA has WB == 0, WA == WC == 0.5.
    const Ray ray{{0.0f, 0.5f, -1.0f}, {0.0f, 0.0f, 1.0f}};
    const auto classify = RayTriangle_Classify(ray, kA, kB, kC);
    EXPECT_EQ(classify.Kind, IX::Kind::Touching);
    EXPECT_EQ(classify.OnTriangle, IX::TriangleFeature::EdgeCA);
    EXPECT_NEAR(classify.WB, 0.0, 1.0e-5);
}

// -----------------------------------------------------------------------------
// Ray-feature classification.
// -----------------------------------------------------------------------------

TEST(RayTriangleClassifyRayFeature, OriginOnTriangleReportsOriginFeature)
{
    // Ray origin sits on triangle interior; T == 0.
    const Ray ray{{0.25f, 0.25f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    const auto classify = RayTriangle_Classify(ray, kA, kB, kC);
    EXPECT_EQ(classify.Kind, IX::Kind::Proper);
    EXPECT_EQ(classify.OnRay, IX::RayFeature::Origin);
    EXPECT_NEAR(classify.RayParam, 0.0, 1.0e-6);
}

TEST(RayTriangleClassifyRayFeature, ForwardHitReportsInteriorRayFeature)
{
    const Ray ray{{0.25f, 0.25f, -2.0f}, {0.0f, 0.0f, 1.0f}};
    const auto classify = RayTriangle_Classify(ray, kA, kB, kC);
    EXPECT_EQ(classify.OnRay, IX::RayFeature::Interior);
    EXPECT_GT(classify.RayParam, 0.0);
}

// -----------------------------------------------------------------------------
// Cross-check parity across an array of arbitrary configurations — guards
// against future kernel changes silently diverging the two entry points.
// -----------------------------------------------------------------------------

TEST(RayTriangleClassifyParity, BatchedConfigurationsAgreeOnHitGeometry)
{
    struct Config
    {
        Ray Ray;
        glm::vec3 A;
        glm::vec3 B;
        glm::vec3 C;
    };

    const Config configs[] = {
        {{{0.10f, 0.10f, -1.0f}, {0.0f, 0.0f, 1.0f}}, kA, kB, kC},
        {{{0.40f, 0.30f, -2.0f}, {0.0f, 0.0f, 1.0f}}, kA, kB, kC},
        {{{0.00f, 0.00f, -1.0f}, {0.1f, 0.1f, 1.0f}}, kA, kB, kC},
        {{{0.50f, 0.25f, 5.0f}, {0.0f, 0.0f, -1.0f}}, kA, kB, kC},
        {{{1.00f, 1.00f, -3.0f}, {-0.3f, -0.3f, 1.0f}}, kA, kB, kC},
    };

    for (const auto& c : configs)
    {
        const auto watertight = RayTriangle_Watertight(c.Ray, c.A, c.B, c.C);
        const auto classify = RayTriangle_Classify(c.Ray, c.A, c.B, c.C);

        if (watertight.has_value())
        {
            ASSERT_NE(classify.Kind, IX::Kind::None);
            ASSERT_NE(classify.Kind, IX::Kind::DegenerateInput);
            ExpectGeometryParity(classify, *watertight);
        }
        else
        {
            EXPECT_EQ(classify.Kind, IX::Kind::None);
        }
    }
}

