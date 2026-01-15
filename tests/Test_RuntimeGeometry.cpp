// tests/Test_RuntimeGeometry_SDF.cpp
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <random>

import Geometry;

using namespace Geometry;
using namespace Geometry::Validation;

// --- Helper for approximate vector equality ---
inline void ExpectVec3Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 0.01f)
{
    EXPECT_NEAR(a.x, b.x, tolerance) << "Expected " << glm::to_string(b) << ", got " << glm::to_string(a);
    EXPECT_NEAR(a.y, b.y, tolerance);
    EXPECT_NEAR(a.z, b.z, tolerance);
}

TEST(GeometryProperties, NoRTTI_System)
{
    PropertySet vertices;
    vertices.Resize(3); // Triangle

    // Add Dynamic Property
    auto colorProp = vertices.Add<glm::vec3>("Color", {1,1,1});
    auto weightProp = vertices.Add<float>("Weight", 0.0f);

    EXPECT_TRUE(colorProp.IsValid());
    EXPECT_TRUE(weightProp.IsValid());

    // Modify Data
    colorProp[0] = glm::vec3{1.0f, 0.0f, 0.0f};
    weightProp[1] = 0.5f;

    // Retrieve by Name
    auto fetchedProp = vertices.Get<glm::vec3>("Color");
    EXPECT_TRUE(fetchedProp.IsValid());
    EXPECT_EQ(fetchedProp[0].x, 1.0f);

    // Type Safety Check (Try to get float as vec3)
    auto invalidProp = vertices.Get<glm::vec3>("Weight");
    EXPECT_FALSE(invalidProp.IsValid());
}

TEST(SDF_Solver, Sphere_Vs_Sphere)
{
    Sphere s1{{0,0,0}, 1.0f};
    Sphere s2{{1.5f,0,0}, 1.0f}; // Overlap by 0.5

    auto sdf1 = SDF::CreateSDF(s1);
    auto sdf2 = SDF::CreateSDF(s2);

    // Initial guess: Midpoint
    glm::vec3 guess = (s1.Center + s2.Center) * 0.5f;

    auto result = SDF::Contact_General_SDF(sdf1, sdf2, guess);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->PenetrationDepth, 0.5f, 0.05f);
    ExpectVec3Near(result->Normal, {1, 0, 0});
}

TEST(SDF_Solver, OBB_Vs_Sphere_Deep)
{
    // A rotated box
    OBB box;
    box.Center = {0, 0, 0};
    box.Extents = {1, 1, 1};
    box.Rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 0, 1)); // 45 deg Z

    // Sphere penetrating the corner
    // Unrotated box corner is at roughly (1.414, 0, 0)
    Sphere s{{1.0f, 0, 0}, 0.5f};

    auto sdfBox = SDF::CreateSDF(box);
    auto sdfSphere = SDF::CreateSDF(s);

    glm::vec3 guess = (box.Center + s.Center) * 0.5f;
    auto result = SDF::Contact_General_SDF(sdfBox, sdfSphere, guess);

    ASSERT_TRUE(result.has_value());

    // Normal A->B (Box -> Sphere).
    // Box center (0,0,0). Sphere center (1,0,0).
    // Normal should point roughly +X.
    EXPECT_GT(result->Normal.x, 0.5f);
}

TEST(SDF_Solver, Capsule_Vs_Box)
{
    // Vertical Capsule at origin
    Capsule cap{{0, -1, 0}, {0, 1, 0}, 0.5f};

    // Box hitting it from the side
    OBB box;
    box.Center = {0.8f, 0, 0};
    box.Extents = {0.5f, 0.5f, 0.5f};
    box.Rotation = glm::quat(1,0,0,0);

    // Gap check:
    // Capsule surface at x=0.5.
    // Box surface at 0.8 - 0.5 = 0.3.
    // Overlap = 0.5 - 0.3 = 0.2.

    auto sdfCap = SDF::CreateSDF(cap);
    auto sdfBox = SDF::CreateSDF(box);

    auto result = SDF::Contact_General_SDF(sdfCap, sdfBox, {0.4f, 0, 0});

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->PenetrationDepth, 0.2f, 0.05f);
    // Normal should be along X axis
    EXPECT_NEAR(std::abs(result->Normal.x), 1.0f, 0.01f);
}

TEST(SDF_Solver, No_Overlap)
{
    Sphere s1{{0,0,0}, 1.0f};
    Sphere s2{{3.0f,0,0}, 1.0f};

    auto sdf1 = SDF::CreateSDF(s1);
    auto sdf2 = SDF::CreateSDF(s2);

    auto result = SDF::Contact_General_SDF(sdf1, sdf2, {1.5f, 0, 0});
    EXPECT_FALSE(result.has_value());
}

TEST(SDF_Solver, Sphere_Vs_Triangle)
{
    // Triangle on the floor
    Triangle t{
            {-2, 0, -2},
            { 2, 0, -2},
            { 0, 0,  2}
    };

    // Sphere falling onto it
    Sphere s{{0, 0.5f, 0}, 1.0f};

    auto sdfTri = SDF::CreateSDF(t);
    auto sdfSphere = SDF::CreateSDF(s);

    auto result = SDF::Contact_General_SDF(sdfTri, sdfSphere, {0, 0.2f, 0});

    ASSERT_TRUE(result.has_value());
    // Sphere Radius 1.0. Center Y=0.5. Floor Y=0.
    // Penetration = 0.5.
    EXPECT_NEAR(result->PenetrationDepth, 0.5f, 0.05f);
    // Normal should be Up (0, 1, 0)
    // Note: Sign depends on A-B vs B-A convention.
    EXPECT_NEAR(std::abs(result->Normal.y), 1.0f, 0.05f);
}


// -----------------------------------------------------------------------------
// Vector Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, IsFinite_ValidVector)
{
    EXPECT_TRUE(IsFinite(glm::vec3(1.0f, 2.0f, 3.0f)));
    EXPECT_TRUE(IsFinite(glm::vec3(0.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(IsFinite(glm::vec3(-1e20f, 1e20f, 0.0f)));
}

TEST(GeometryValidation, IsFinite_NaN)
{
    float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsFinite(glm::vec3(nan, 0.0f, 0.0f)));
    EXPECT_FALSE(IsFinite(glm::vec3(0.0f, nan, 0.0f)));
    EXPECT_FALSE(IsFinite(glm::vec3(0.0f, 0.0f, nan)));
}

TEST(GeometryValidation, IsFinite_Infinity)
{
    float inf = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsFinite(glm::vec3(inf, 0.0f, 0.0f)));
    EXPECT_FALSE(IsFinite(glm::vec3(0.0f, -inf, 0.0f)));
}

TEST(GeometryValidation, IsNormalized_UnitVectors)
{
    EXPECT_TRUE(IsNormalized(glm::vec3(1.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(IsNormalized(glm::vec3(0.0f, 1.0f, 0.0f)));
    EXPECT_TRUE(IsNormalized(glm::vec3(0.0f, 0.0f, 1.0f)));

    glm::vec3 diagonal = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(IsNormalized(diagonal));
}

TEST(GeometryValidation, IsNormalized_NonUnitVectors)
{
    EXPECT_FALSE(IsNormalized(glm::vec3(2.0f, 0.0f, 0.0f)));
    EXPECT_FALSE(IsNormalized(glm::vec3(0.5f, 0.0f, 0.0f)));
    EXPECT_FALSE(IsNormalized(glm::vec3(0.0f, 0.0f, 0.0f)));
}

TEST(GeometryValidation, IsZero_ZeroVector)
{
    EXPECT_TRUE(IsZero(glm::vec3(0.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(IsZero(glm::vec3(1e-10f, 1e-10f, 1e-10f)));
}

TEST(GeometryValidation, IsZero_NonZeroVector)
{
    EXPECT_FALSE(IsZero(glm::vec3(1.0f, 0.0f, 0.0f)));
    EXPECT_FALSE(IsZero(glm::vec3(0.1f, 0.0f, 0.0f)));
}

// -----------------------------------------------------------------------------
// Sphere Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, Sphere_Valid)
{
    Sphere s{{0, 0, 0}, 1.0f};
    EXPECT_TRUE(IsValid(s));

    Sphere s2{{100, -50, 25}, 0.001f};
    EXPECT_TRUE(IsValid(s2));
}

TEST(GeometryValidation, Sphere_Invalid_ZeroRadius)
{
    Sphere s{{0, 0, 0}, 0.0f};
    EXPECT_FALSE(IsValid(s));
}

TEST(GeometryValidation, Sphere_Invalid_NegativeRadius)
{
    Sphere s{{0, 0, 0}, -1.0f};
    EXPECT_FALSE(IsValid(s));
}

TEST(GeometryValidation, Sphere_Invalid_InfiniteRadius)
{
    Sphere s{{0, 0, 0}, std::numeric_limits<float>::infinity()};
    EXPECT_FALSE(IsValid(s));
}

TEST(GeometryValidation, Sphere_Invalid_NaNCenter)
{
    Sphere s{{std::numeric_limits<float>::quiet_NaN(), 0, 0}, 1.0f};
    EXPECT_FALSE(IsValid(s));
}

// -----------------------------------------------------------------------------
// AABB Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, AABB_Valid)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    EXPECT_TRUE(IsValid(box));
}

TEST(GeometryValidation, AABB_Invalid_Inverted)
{
    AABB box{{1, 1, 1}, {-1, -1, -1}};  // Min > Max
    EXPECT_FALSE(IsValid(box));
}

TEST(GeometryValidation, AABB_Valid_Degenerate)
{
    AABB box{{0, 0, 0}, {0, 0, 0}};  // Point box - valid but degenerate
    EXPECT_TRUE(IsValid(box));
    EXPECT_TRUE(IsDegenerate(box));
}

TEST(GeometryValidation, AABB_NotDegenerate)
{
    AABB box{{0, 0, 0}, {1, 1, 1}};
    EXPECT_FALSE(IsDegenerate(box));
}

TEST(GeometryValidation, AABB_Degenerate_FlatBox)
{
    AABB box{{0, 0, 0}, {1, 1, 0}};  // Flat in Z
    EXPECT_TRUE(IsDegenerate(box));
}

// -----------------------------------------------------------------------------
// OBB Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, OBB_Valid)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {1, 1, 1};
    obb.Rotation = glm::quat(1, 0, 0, 0);  // Identity

    EXPECT_TRUE(IsValid(obb));
}

TEST(GeometryValidation, OBB_Invalid_ZeroExtent)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {0, 1, 1};
    obb.Rotation = glm::quat(1, 0, 0, 0);

    EXPECT_FALSE(IsValid(obb));
}

TEST(GeometryValidation, OBB_Invalid_UnnormalizedRotation)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {1, 1, 1};
    obb.Rotation = glm::quat(2, 0, 0, 0);  // Not normalized

    EXPECT_FALSE(IsValid(obb));
}

TEST(GeometryValidation, OBB_Degenerate)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {1e-8f, 1, 1};  // Nearly zero X extent
    obb.Rotation = glm::quat(1, 0, 0, 0);

    EXPECT_TRUE(IsDegenerate(obb));
}

// -----------------------------------------------------------------------------
// Capsule Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, Capsule_Valid)
{
    Capsule cap{{0, -1, 0}, {0, 1, 0}, 0.5f};
    EXPECT_TRUE(IsValid(cap));
}

TEST(GeometryValidation, Capsule_Invalid_ZeroRadius)
{
    Capsule cap{{0, -1, 0}, {0, 1, 0}, 0.0f};
    EXPECT_FALSE(IsValid(cap));
}

TEST(GeometryValidation, Capsule_Degenerate_SameEndpoints)
{
    Capsule cap{{0, 0, 0}, {0, 0, 0}, 1.0f};
    EXPECT_TRUE(IsDegenerate(cap));  // Line has zero length
}

// -----------------------------------------------------------------------------
// Triangle Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, Triangle_Valid)
{
    Triangle tri{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    EXPECT_TRUE(IsValid(tri));
    EXPECT_FALSE(IsDegenerate(tri));
}

TEST(GeometryValidation, Triangle_Degenerate_Collinear)
{
    Triangle tri{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};  // All on X-axis
    EXPECT_TRUE(IsDegenerate(tri));
}

TEST(GeometryValidation, Triangle_Degenerate_Coincident)
{
    Triangle tri{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};  // All at origin
    EXPECT_TRUE(IsDegenerate(tri));
}

// -----------------------------------------------------------------------------
// Ray Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, Ray_Valid)
{
    Ray r{{0, 0, 0}, {1, 0, 0}};
    EXPECT_TRUE(IsValid(r));
}

TEST(GeometryValidation, Ray_Invalid_ZeroDirection)
{
    Ray r{{0, 0, 0}, {0, 0, 0}};
    EXPECT_FALSE(IsValid(r));
}

TEST(GeometryValidation, Ray_Invalid_NaNOrigin)
{
    Ray r{{std::numeric_limits<float>::quiet_NaN(), 0, 0}, {1, 0, 0}};
    EXPECT_FALSE(IsValid(r));
}

// -----------------------------------------------------------------------------
// Plane Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, Plane_Valid)
{
    Plane p{glm::vec3(0, 1, 0), 5.0f};
    EXPECT_TRUE(IsValid(p));
}

TEST(GeometryValidation, Plane_Invalid_ZeroNormal)
{
    Plane p{glm::vec3(0, 0, 0), 1.0f};
    EXPECT_FALSE(IsValid(p));
}

TEST(GeometryValidation, Plane_Invalid_NaNDistance)
{
    Plane p{glm::vec3(0, 1, 0), std::numeric_limits<float>::quiet_NaN()};
    EXPECT_FALSE(IsValid(p));
}

// -----------------------------------------------------------------------------
// Sanitization Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, Sanitize_Sphere_Valid)
{
    Sphere s{{1, 2, 3}, 5.0f};
    Sphere sanitized = Sanitize(s);

    EXPECT_EQ(sanitized.Center, s.Center);
    EXPECT_EQ(sanitized.Radius, s.Radius);
}

TEST(GeometryValidation, Sanitize_Sphere_Invalid)
{
    Sphere s{{std::numeric_limits<float>::quiet_NaN(), 0, 0}, -1.0f};
    Sphere sanitized = Sanitize(s);

    EXPECT_TRUE(IsValid(sanitized));
    EXPECT_EQ(sanitized.Center, glm::vec3(0.0f));
    EXPECT_EQ(sanitized.Radius, 1.0f);
}

TEST(GeometryValidation, Sanitize_AABB_Inverted)
{
    AABB box{{10, 10, 10}, {0, 0, 0}};
    AABB sanitized = Sanitize(box);

    EXPECT_TRUE(IsValid(sanitized));
    EXPECT_LE(sanitized.Min.x, sanitized.Max.x);
    EXPECT_LE(sanitized.Min.y, sanitized.Max.y);
    EXPECT_LE(sanitized.Min.z, sanitized.Max.z);
}

TEST(GeometryValidation, Sanitize_Ray_ZeroDirection)
{
    Ray r{{5, 5, 5}, {0, 0, 0}};
    Ray sanitized = Sanitize(r);

    EXPECT_TRUE(IsValid(sanitized));
    EXPECT_EQ(sanitized.Origin, glm::vec3(5, 5, 5));  // Origin preserved
    EXPECT_NE(sanitized.Direction, glm::vec3(0, 0, 0));
    EXPECT_TRUE(IsNormalized(sanitized.Direction));
}

TEST(GeometryValidation, Sanitize_OBB_UnnormalizedRotation)
{
    OBB obb;
    obb.Center = {1, 2, 3};
    obb.Extents = {1, 1, 1};
    obb.Rotation = glm::quat(10, 5, 3, 1);  // Not normalized

    OBB sanitized = Sanitize(obb);

    EXPECT_TRUE(IsValid(sanitized));
    // Check quaternion is normalized: w^2 + x^2 + y^2 + z^2 = 1
    float rotLenSq = sanitized.Rotation.w * sanitized.Rotation.w +
                     sanitized.Rotation.x * sanitized.Rotation.x +
                     sanitized.Rotation.y * sanitized.Rotation.y +
                     sanitized.Rotation.z * sanitized.Rotation.z;
    EXPECT_NEAR(rotLenSq, 1.0f, 1e-4f);
}


// -----------------------------------------------------------------------------
// Helper Functions Octree
// -----------------------------------------------------------------------------

std::vector<AABB> GenerateRandomAABBs(size_t count, float worldSize, float maxBoxSize, unsigned seed = 42)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> posDist(-worldSize / 2, worldSize / 2);
    std::uniform_real_distribution<float> sizeDist(0.1f, maxBoxSize);

    std::vector<AABB> result;
    result.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        glm::vec3 center(posDist(rng), posDist(rng), posDist(rng));
        glm::vec3 halfSize(sizeDist(rng), sizeDist(rng), sizeDist(rng));
        result.push_back(AABB{center - halfSize, center + halfSize});
    }

    return result;
}

std::vector<AABB> GenerateGridAABBs(int gridSize, float spacing)
{
    std::vector<AABB> result;
    result.reserve(gridSize * gridSize * gridSize);

    float boxSize = spacing * 0.8f;  // Slight gap between boxes

    for (int x = 0; x < gridSize; ++x)
    {
        for (int y = 0; y < gridSize; ++y)
        {
            for (int z = 0; z < gridSize; ++z)
            {
                glm::vec3 center(x * spacing, y * spacing, z * spacing);
                glm::vec3 halfSize(boxSize * 0.5f);
                result.push_back(AABB{center - halfSize, center + halfSize});
            }
        }
    }

    return result;
}

// -----------------------------------------------------------------------------
// Build Tests Octree
// -----------------------------------------------------------------------------

TEST(Octree, Build_EmptyInput)
{
    Octree octree;
    std::vector<AABB> empty;

    Octree::SplitPolicy policy;
    bool success = octree.Build(empty, policy, 8, 10);

    EXPECT_FALSE(success);
}

TEST(Octree, Build_SingleElement)
{
    Octree octree;
    std::vector<AABB> aabbs = {AABB{{0, 0, 0}, {1, 1, 1}}};

    Octree::SplitPolicy policy;
    bool success = octree.Build(aabbs, policy, 8, 10);

    EXPECT_TRUE(success);
    EXPECT_EQ(octree.m_Nodes.size(), 1u);  // Just root
    EXPECT_TRUE(octree.m_Nodes[0].IsLeaf);
}

TEST(Octree, Build_SmallSet)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(10, 100.0f, 5.0f);

    Octree::SplitPolicy policy;
    bool success = octree.Build(aabbs, policy, 4, 10);

    EXPECT_TRUE(success);
    EXPECT_TRUE(octree.ValidateStructure());
}

TEST(Octree, Build_LargeSet)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(1000, 100.0f, 2.0f);

    Octree::SplitPolicy policy;
    policy.SplitPoint = Octree::SplitPoint::Median;
    bool success = octree.Build(aabbs, policy, 8, 10);

    EXPECT_TRUE(success);
    EXPECT_TRUE(octree.ValidateStructure());
}

TEST(Octree, Build_DifferentSplitPolicies)
{
    auto aabbs = GenerateRandomAABBs(100, 50.0f, 3.0f);

    // Test Center split
    {
        Octree octree;
        Octree::SplitPolicy policy;
        policy.SplitPoint = Octree::SplitPoint::Center;
        EXPECT_TRUE(octree.Build(aabbs, policy, 8, 10));
        EXPECT_TRUE(octree.ValidateStructure());
    }

    // Test Mean split
    {
        Octree octree;
        Octree::SplitPolicy policy;
        policy.SplitPoint = Octree::SplitPoint::Mean;
        EXPECT_TRUE(octree.Build(aabbs, policy, 8, 10));
        EXPECT_TRUE(octree.ValidateStructure());
    }

    // Test Median split
    {
        Octree octree;
        Octree::SplitPolicy policy;
        policy.SplitPoint = Octree::SplitPoint::Median;
        EXPECT_TRUE(octree.Build(aabbs, policy, 8, 10));
        EXPECT_TRUE(octree.ValidateStructure());
    }
}

// -----------------------------------------------------------------------------
// AABB Query Tests
// -----------------------------------------------------------------------------

TEST(Octree, QueryAABB_EmptyResult)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(100, 50.0f, 2.0f);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    // Query far outside the data
    AABB query{{1000, 1000, 1000}, {1001, 1001, 1001}};
    std::vector<size_t> results;
    octree.QueryAABB(query, results);

    EXPECT_TRUE(results.empty());
}

TEST(Octree, QueryAABB_AllElements)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(50, 10.0f, 1.0f);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    // Query encompassing all elements
    AABB query{{-100, -100, -100}, {100, 100, 100}};
    std::vector<size_t> results;
    octree.QueryAABB(query, results);

    EXPECT_EQ(results.size(), aabbs.size());
}

TEST(Octree, QueryAABB_PartialOverlap)
{
    Octree octree;
    auto aabbs = GenerateGridAABBs(5, 2.0f);  // 125 boxes in 5x5x5 grid

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    // Query should hit a subset
    AABB query{{0, 0, 0}, {4, 4, 4}};  // Should hit ~27 boxes (3x3x3 region)
    std::vector<size_t> results;
    octree.QueryAABB(query, results);

    EXPECT_GT(results.size(), 0u);
    EXPECT_LT(results.size(), aabbs.size());

    // Verify all results actually overlap
    for (size_t idx : results)
    {
        EXPECT_TRUE(TestOverlap(aabbs[idx], query));
    }
}

TEST(Octree, QueryAABB_CorrectResults)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(200, 50.0f, 2.0f, 123);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    AABB query{{-10, -10, -10}, {10, 10, 10}};
    std::vector<size_t> octreeResults;
    octree.QueryAABB(query, octreeResults);

    // Brute force check
    std::vector<size_t> bruteForceResults;
    for (size_t i = 0; i < aabbs.size(); ++i)
    {
        if (TestOverlap(aabbs[i], query))
        {
            bruteForceResults.push_back(i);
        }
    }

    std::sort(octreeResults.begin(), octreeResults.end());
    std::sort(bruteForceResults.begin(), bruteForceResults.end());

    EXPECT_EQ(octreeResults, bruteForceResults);
}

// -----------------------------------------------------------------------------
// Sphere Query Tests
// -----------------------------------------------------------------------------

TEST(Octree, QuerySphere_Basic)
{
    Octree octree;
    auto aabbs = GenerateGridAABBs(5, 2.0f);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    Sphere query{{4, 4, 4}, 3.0f};
    std::vector<size_t> results;
    octree.QuerySphere(query, results);

    EXPECT_GT(results.size(), 0u);

    // Verify correctness
    for (size_t idx : results)
    {
        EXPECT_TRUE(TestOverlap(aabbs[idx], query));
    }
}

TEST(Octree, QuerySphere_CorrectResults)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(150, 40.0f, 2.0f, 456);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    Sphere query{{0, 0, 0}, 10.0f};
    std::vector<size_t> octreeResults;
    octree.QuerySphere(query, octreeResults);

    // Brute force
    std::vector<size_t> bruteForceResults;
    for (size_t i = 0; i < aabbs.size(); ++i)
    {
        if (TestOverlap(aabbs[i], query))
        {
            bruteForceResults.push_back(i);
        }
    }

    std::sort(octreeResults.begin(), octreeResults.end());
    std::sort(bruteForceResults.begin(), bruteForceResults.end());

    EXPECT_EQ(octreeResults, bruteForceResults);
}

// -----------------------------------------------------------------------------
// Ray Query Tests
// -----------------------------------------------------------------------------

TEST(Octree, QueryRay_Basic)
{
    Octree octree;
    auto aabbs = GenerateGridAABBs(5, 2.0f);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    Ray query{{-10, 2, 2}, glm::normalize(glm::vec3(1, 0, 0))};
    std::vector<size_t> results;
    octree.QueryRay(query, results);

    // Ray along X at Y=2, Z=2 should hit several boxes
    EXPECT_GT(results.size(), 0u);
}

TEST(Octree, QueryRay_Miss)
{
    Octree octree;
    std::vector<AABB> aabbs = {AABB{{0, 0, 0}, {1, 1, 1}}};

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    // Ray that misses the box
    Ray query{{10, 10, 10}, glm::normalize(glm::vec3(1, 0, 0))};
    std::vector<size_t> results;
    octree.QueryRay(query, results);

    EXPECT_TRUE(results.empty());
}

// -----------------------------------------------------------------------------
// Nearest Neighbor Query Tests
// -----------------------------------------------------------------------------

TEST(Octree, QueryNearest_Basic)
{
    Octree octree;
    std::vector<AABB> aabbs = {
        AABB{{0, 0, 0}, {1, 1, 1}},
        AABB{{10, 10, 10}, {11, 11, 11}},
        AABB{{-20, 0, 0}, {-19, 1, 1}}
    };

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    glm::vec3 queryPoint{0.5f, 0.5f, 0.5f};
    size_t result;
    octree.QueryNearest(queryPoint, result);

    EXPECT_EQ(result, 0u);  // First box contains the point
}

TEST(Octree, QueryNearest_CorrectResult)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(100, 50.0f, 2.0f, 789);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    glm::vec3 queryPoint{5.0f, 5.0f, 5.0f};
    size_t octreeResult;
    octree.QueryNearest(queryPoint, octreeResult);

    // Brute force find nearest
    double minDistSq = std::numeric_limits<double>::max();
    size_t bruteForceResult = 0;
    for (size_t i = 0; i < aabbs.size(); ++i)
    {
        double distSq = SquaredDistance(aabbs[i], queryPoint);
        if (distSq < minDistSq)
        {
            minDistSq = distSq;
            bruteForceResult = i;
        }
    }

    EXPECT_EQ(octreeResult, bruteForceResult);
}

// -----------------------------------------------------------------------------
// KNN Query Tests
// -----------------------------------------------------------------------------

TEST(Octree, QueryKnn_Basic)
{
    Octree octree;
    std::vector<AABB> aabbs = {
        AABB{{0, 0, 0}, {1, 1, 1}},      // Closest to origin
        AABB{{3, 0, 0}, {4, 1, 1}},      // Second
        AABB{{6, 0, 0}, {7, 1, 1}},      // Third
        AABB{{10, 0, 0}, {11, 1, 1}},    // Fourth
        AABB{{20, 0, 0}, {21, 1, 1}}     // Fifth
    };

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    glm::vec3 queryPoint{0.5f, 0.5f, 0.5f};
    std::vector<size_t> results;
    octree.QueryKnn(queryPoint, 3, results);

    ASSERT_EQ(results.size(), 3u);
    // Results should be sorted by distance (closest first)
    EXPECT_EQ(results[0], 0u);  // Closest
    EXPECT_EQ(results[1], 1u);  // Second
    EXPECT_EQ(results[2], 2u);  // Third
}

TEST(Octree, QueryKnn_KGreaterThanElements)
{
    Octree octree;
    std::vector<AABB> aabbs = {
        AABB{{0, 0, 0}, {1, 1, 1}},
        AABB{{5, 0, 0}, {6, 1, 1}}
    };

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    std::vector<size_t> results;
    octree.QueryKnn({0, 0, 0}, 10, results);  // Ask for 10, only 2 exist

    EXPECT_EQ(results.size(), 2u);
}

TEST(Octree, QueryKnn_CorrectResults)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(100, 50.0f, 2.0f, 321);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    glm::vec3 queryPoint{0, 0, 0};
    const size_t k = 5;

    std::vector<size_t> octreeResults;
    octree.QueryKnn(queryPoint, k, octreeResults);

    // Brute force KNN
    std::vector<std::pair<double, size_t>> allDistances;
    for (size_t i = 0; i < aabbs.size(); ++i)
    {
        allDistances.emplace_back(SquaredDistance(aabbs[i], queryPoint), i);
    }
    std::sort(allDistances.begin(), allDistances.end());

    std::vector<size_t> bruteForceResults;
    for (size_t i = 0; i < k && i < allDistances.size(); ++i)
    {
        bruteForceResults.push_back(allDistances[i].second);
    }

    EXPECT_EQ(octreeResults, bruteForceResults);
}

// -----------------------------------------------------------------------------
// Node Property Tests
// -----------------------------------------------------------------------------

TEST(Octree, AddNodeProperty)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(50, 20.0f, 2.0f);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    auto floatProp = octree.AddNodeProperty<float>("Density", 0.0f);
    EXPECT_TRUE(floatProp.IsValid());

    // Set some values using NodeHandle
    NodeHandle node0{0};
    floatProp[node0] = 1.5f;
    EXPECT_FLOAT_EQ(floatProp[node0], 1.5f);
}

TEST(Octree, GetNodeProperty)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(20, 10.0f, 1.0f);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    [[maybe_unused]] auto _ = octree.AddNodeProperty<int>("Count", 42);

    auto prop = octree.GetNodeProperty<int>("Count");
    EXPECT_TRUE(prop.IsValid());

    NodeHandle node0{0};
    EXPECT_EQ(prop[node0], 42);  // Default value
}

TEST(Octree, HasNodeProperty)
{
    Octree octree;
    auto aabbs = GenerateRandomAABBs(10, 5.0f, 1.0f);

    Octree::SplitPolicy policy;
    ASSERT_TRUE(octree.Build(aabbs, policy, 8, 10));

    EXPECT_FALSE(octree.HasNodeProperty("Custom"));

    [[maybe_unused]] auto _ = octree.AddNodeProperty<float>("Custom", 0.0f);

    EXPECT_TRUE(octree.HasNodeProperty("Custom"));
}

// -----------------------------------------------------------------------------
// Edge Cases
// -----------------------------------------------------------------------------

TEST(Octree, AllElementsAtSamePoint)
{
    Octree octree;
    std::vector<AABB> aabbs;
    for (int i = 0; i < 100; ++i)
    {
        aabbs.push_back(AABB{{0, 0, 0}, {0.001f, 0.001f, 0.001f}});
    }

    Octree::SplitPolicy policy;
    bool success = octree.Build(aabbs, policy, 8, 10);

    EXPECT_TRUE(success);
    EXPECT_TRUE(octree.ValidateStructure());
}

TEST(Octree, LargeExtentDifferences)
{
    Octree octree;
    std::vector<AABB> aabbs = {
        AABB{{0, 0, 0}, {0.001f, 0.001f, 0.001f}},          // Tiny
        AABB{{-1000, -1000, -1000}, {1000, 1000, 1000}}     // Huge
    };

    Octree::SplitPolicy policy;
    bool success = octree.Build(aabbs, policy, 8, 10);

    EXPECT_TRUE(success);

    // Query should find both when encompassing
    std::vector<size_t> results;
    octree.QueryAABB(AABB{{-2000, -2000, -2000}, {2000, 2000, 2000}}, results);
    EXPECT_EQ(results.size(), 2u);
}
