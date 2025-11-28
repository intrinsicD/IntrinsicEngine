#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Runtime.Geometry.Properties;

using namespace Runtime::Geometry;

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