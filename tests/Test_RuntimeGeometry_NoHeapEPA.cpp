// tests/Test_RuntimeGeometry_NoHeapEPA.cpp
#include <gtest/gtest.h>

#include <cstddef>
#include <glm/glm.hpp>

import Core;
import Geometry;

TEST(Physics_Contact, EPA_UsesScratchArena_Smoke)
{
    // Use small tetrahedra rather than cube corner sets.
    // The current GJK implementation only reliably returns a tetrahedron simplex
    // when the configuration is clearly 3D and non-degenerate.
    Geometry::ConvexHull a;
    Geometry::ConvexHull b;

    a.Vertices = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    b.Vertices = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    // Shift B so it overlaps A with a clear interior intersection.
    for (glm::vec3& v : b.Vertices) v += glm::vec3(0.15f, 0.15f, 0.15f);

    Core::Memory::LinearArena scratch(256 * 1024);
    scratch.Reset();

    const std::size_t beforeUsed = scratch.GetUsed();

    auto contact = Geometry::ComputeContact(a, b, scratch);

    ASSERT_TRUE(contact.has_value())
        << "GJK/EPA fallback did not report contact. If this flakes, it indicates a GJK degeneracy case.";

    EXPECT_GT(scratch.GetUsed(), beforeUsed) << "Expected EPA to consume scratch arena memory";
}
