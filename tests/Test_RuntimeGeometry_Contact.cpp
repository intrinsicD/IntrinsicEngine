// tests/Test_RuntimeGeometry_SDF.cpp
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

import Runtime.Geometry.Primitives;
import Runtime.Geometry.Contact;
import Runtime.Geometry.SDF;
import Runtime.Geometry.SDF.General;

using namespace Runtime::Geometry;

// --- Helper for approximate vector equality ---
inline void ExpectVec3Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 0.01f)
{
    EXPECT_NEAR(a.x, b.x, tolerance) << "Expected " << glm::to_string(b) << ", got " << glm::to_string(a);
    EXPECT_NEAR(a.y, b.y, tolerance);
    EXPECT_NEAR(a.z, b.z, tolerance);
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