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
    ps.Add<float>("curvature", 0.0f);

    auto props = EnumerateColorableProperties(ps);
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].Name, "curvature");
    EXPECT_EQ(props[0].Type, PropertyDataType::Scalar);
}

TEST(PropertyEnumerator, EnumeratesVec3Property)
{
    Geometry::PropertySet ps;
    ps.Add<glm::vec3>("v:color", glm::vec3(0.0f));

    auto props = EnumerateColorableProperties(ps);
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].Name, "v:color");
    EXPECT_EQ(props[0].Type, PropertyDataType::Vec3);
}

TEST(PropertyEnumerator, EnumeratesVec4Property)
{
    Geometry::PropertySet ps;
    ps.Add<glm::vec4>("rgba", glm::vec4(0.0f));

    auto props = EnumerateColorableProperties(ps);
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].Name, "rgba");
    EXPECT_EQ(props[0].Type, PropertyDataType::Vec4);
}

TEST(PropertyEnumerator, FiltersInternalProperties)
{
    Geometry::PropertySet ps;
    ps.Add<glm::vec3>("v:position", glm::vec3(0.0f));
    ps.Add<glm::vec3>("v:normal", glm::vec3(0.0f));
    ps.Add<glm::vec3>("p:position", glm::vec3(0.0f));
    ps.Add<glm::vec3>("p:normal", glm::vec3(0.0f));
    ps.Add<float>("curvature", 0.0f);

    auto props = EnumerateColorableProperties(ps);
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].Name, "curvature");
}

TEST(PropertyEnumerator, EnumerateScalarOnlyReturnsFloat)
{
    Geometry::PropertySet ps;
    ps.Add<float>("curvature", 0.0f);
    ps.Add<glm::vec3>("v:color", glm::vec3(0.0f));
    ps.Add<glm::vec4>("rgba", glm::vec4(0.0f));

    auto props = EnumerateScalarProperties(ps);
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].Name, "curvature");
    EXPECT_EQ(props[0].Type, PropertyDataType::Scalar);
}

TEST(PropertyEnumerator, EnumerateVectorOnlyReturnsVec3)
{
    Geometry::PropertySet ps;
    ps.Add<float>("curvature", 0.0f);
    ps.Add<glm::vec3>("direction", glm::vec3(0.0f));
    ps.Add<glm::vec4>("rgba", glm::vec4(0.0f));

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
    ps.Add<float>("curvature", 0.0f);
    ps.Add<float>("area", 0.0f);
    ps.Add<glm::vec3>("v:color", glm::vec3(0.0f));

    auto props = EnumerateColorableProperties(ps);
    EXPECT_EQ(props.size(), 3u);
}

TEST(PropertyEnumerator, UnsupportedTypeFiltered)
{
    Geometry::PropertySet ps;
    ps.Add<int>("label", 0);
    ps.Add<float>("curvature", 0.0f);

    auto props = EnumerateColorableProperties(ps);
    // int is not float/vec3/vec4, so only curvature shows up.
    ASSERT_EQ(props.size(), 1u);
    EXPECT_EQ(props[0].Name, "curvature");
}
