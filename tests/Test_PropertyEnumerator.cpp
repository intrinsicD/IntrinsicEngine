#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

using namespace Graphics;

// =============================================================================
// PropertyEnumerator — unit tests for PropertySet property discovery.
// =============================================================================

TEST(PropertyEnumerator, EnumeratesFloatProperty)
{
    Geometry::PropertySet ps;
    (void)ps.Add<float>("curvature", 0.0f);

    auto props = EnumerateColorableProperties(ps);
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].Name, "curvature");
    EXPECT_EQ(props[0].Type, PropertyDataType::Scalar);
}

TEST(PropertyEnumerator, EnumeratesVec3Property)
{
    Geometry::PropertySet ps;
    (void)ps.Add<glm::vec3>("v:color", glm::vec3(0.0f));

    auto props = EnumerateColorableProperties(ps);
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].Name, "v:color");
    EXPECT_EQ(props[0].Type, PropertyDataType::Vec3);
}

TEST(PropertyEnumerator, EnumeratesVec4Property)
{
    Geometry::PropertySet ps;
    (void)ps.Add<glm::vec4>("rgba", glm::vec4(0.0f));

    auto props = EnumerateColorableProperties(ps);
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].Name, "rgba");
    EXPECT_EQ(props[0].Type, PropertyDataType::Vec4);
}

TEST(PropertyEnumerator, FiltersInternalProperties)
{
    Geometry::PropertySet ps;
    (void)ps.Add<glm::vec3>("v:position", glm::vec3(0.0f));
    (void)ps.Add<glm::vec3>("v:normal", glm::vec3(0.0f));
    (void)ps.Add<glm::vec3>("p:position", glm::vec3(0.0f));
    (void)ps.Add<glm::vec3>("p:normal", glm::vec3(0.0f));
    (void)ps.Add<float>("curvature", 0.0f);

    auto props = EnumerateColorableProperties(ps);
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].Name, "curvature");
}

TEST(PropertyEnumerator, VectorEnumeration_IncludesNormals)
{
    Geometry::PropertySet ps;
    (void)ps.Add<glm::vec3>("v:position", glm::vec3(0.0f));
    (void)ps.Add<glm::vec3>("v:normal", glm::vec3(0.0f));
    (void)ps.Add<glm::vec3>("f:normal", glm::vec3(0.0f));
    (void)ps.Add<glm::vec3>("p:normal", glm::vec3(0.0f));
    (void)ps.Add<glm::vec3>("direction", glm::vec3(0.0f));

    // Normals should appear in vector enumeration (valid vector field sources)
    auto vecProps = EnumerateVectorProperties(ps);
    ASSERT_EQ(vecProps.size(), 4u); // v:normal, f:normal, p:normal, direction
    // v:position should still be filtered out

    // But normals should NOT appear in colorable enumeration
    auto colorProps = EnumerateColorableProperties(ps);
    ASSERT_EQ(colorProps.size(), 1u); // only direction
    EXPECT_EQ(colorProps[0].Name, "direction");
}

TEST(PropertyEnumerator, EnumerateScalarOnlyReturnsFloat)
{
    Geometry::PropertySet ps;
    (void)ps.Add<float>("curvature", 0.0f);
    (void)ps.Add<glm::vec3>("v:color", glm::vec3(0.0f));
    (void)ps.Add<glm::vec4>("rgba", glm::vec4(0.0f));

    auto props = EnumerateScalarProperties(ps);
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].Name, "curvature");
    EXPECT_EQ(props[0].Type, PropertyDataType::Scalar);
}

TEST(PropertyEnumerator, EnumerateVectorOnlyReturnsVec3)
{
    Geometry::PropertySet ps;
    (void)ps.Add<float>("curvature", 0.0f);
    (void)ps.Add<glm::vec3>("direction", glm::vec3(0.0f));
    (void)ps.Add<glm::vec4>("rgba", glm::vec4(0.0f));

    auto props = EnumerateVectorProperties(ps);
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].Name, "direction");
    EXPECT_EQ(props[0].Type, PropertyDataType::Vec3);
}

TEST(PropertyEnumerator, EmptyPropertySet)
{
    Geometry::PropertySet ps;

    auto props = EnumerateColorableProperties(ps);
    EXPECT_TRUE(props.empty());
}

TEST(PropertyEnumerator, MultipleProperties)
{
    Geometry::PropertySet ps;
    (void)ps.Add<float>("curvature", 0.0f);
    (void)ps.Add<float>("area", 0.0f);
    (void)ps.Add<glm::vec3>("v:color", glm::vec3(0.0f));

    auto props = EnumerateColorableProperties(ps);
    EXPECT_EQ(props.size(), 3u);
}

TEST(PropertyEnumerator, UnsupportedTypeFiltered)
{
    Geometry::PropertySet ps;
    (void)ps.Add<int>("label", 0);
    (void)ps.Add<float>("curvature", 0.0f);

    auto props = EnumerateColorableProperties(ps);
    // int is not float/vec3/vec4, so only curvature shows up.
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].Name, "curvature");
}
