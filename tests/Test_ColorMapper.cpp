#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include <optional>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

using namespace Graphics;

// Helper: extract R channel from packed ABGR.
static uint8_t R(uint32_t c) { return static_cast<uint8_t>(c & 0xFF); }
static uint8_t G(uint32_t c) { return static_cast<uint8_t>((c >> 8) & 0xFF); }
static uint8_t B(uint32_t c) { return static_cast<uint8_t>((c >> 16) & 0xFF); }

// =============================================================================
// ColorMapper — unit tests for PropertySet → packed ABGR mapping.
// =============================================================================

TEST(ColorMapper, EmptyPropertyNameReturnsNullopt)
{
    Geometry::PropertySet ps;
    (void)ps.Add<float>("test", 0.0f);

    ColorSource config;
    config.PropertyName = "";

    auto result = ColorMapper::MapProperty(ps, config);
    EXPECT_FALSE(result.has_value());
}

TEST(ColorMapper, NonexistentPropertyReturnsNullopt)
{
    Geometry::PropertySet ps;
    (void)ps.Add<float>("exists", 0.0f);

    ColorSource config;
    config.PropertyName = "does_not_exist";

    auto result = ColorMapper::MapProperty(ps, config);
    EXPECT_FALSE(result.has_value());
}

TEST(ColorMapper, ScalarAutoRange)
{
    Geometry::PropertySet ps;
    auto prop = ps.Add<float>("curvature", 0.0f);
    auto& vec = prop.Vector();
    vec.resize(4);
    vec[0] = 0.0f;
    vec[1] = 1.0f;
    vec[2] = 2.0f;
    vec[3] = 3.0f;

    ColorSource config;
    config.PropertyName = "curvature";
    config.AutoRange = true;
    config.Map = Colormap::Type::Viridis;

    auto result = ColorMapper::MapProperty(ps, config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Colors.size(), 4u);
    EXPECT_FLOAT_EQ(config.RangeMin, 0.0f);
    EXPECT_FLOAT_EQ(config.RangeMax, 3.0f);
}

TEST(ColorMapper, ScalarManualRange)
{
    Geometry::PropertySet ps;
    auto prop = ps.Add<float>("val", 0.0f);
    auto& vec = prop.Vector();
    vec.resize(3);
    vec[0] = 0.0f;
    vec[1] = 5.0f;
    vec[2] = 10.0f;

    ColorSource config;
    config.PropertyName = "val";
    config.AutoRange = false;
    config.RangeMin = 0.0f;
    config.RangeMax = 10.0f;
    config.Map = Colormap::Type::Jet;

    auto result = ColorMapper::MapProperty(ps, config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Colors.size(), 3u);

    // First element maps to t=0 (Jet: dark blue), last to t=1 (Jet: dark red).
    EXPECT_GT(B(result->Colors[0]), R(result->Colors[0]));
    EXPECT_GT(R(result->Colors[2]), B(result->Colors[2]));
}

TEST(ColorMapper, Vec3DirectRGB)
{
    Geometry::PropertySet ps;
    auto prop = ps.Add<glm::vec3>("v:color", glm::vec3(0.0f));
    auto& vec = prop.Vector();
    vec.resize(2);
    vec[0] = glm::vec3(1.0f, 0.0f, 0.0f); // Red
    vec[1] = glm::vec3(0.0f, 0.0f, 1.0f); // Blue

    ColorSource config;
    config.PropertyName = "v:color";

    auto result = ColorMapper::MapProperty(ps, config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Colors.size(), 2u);

    // First should be red: R=255, G~0, B~0
    EXPECT_EQ(R(result->Colors[0]), 255);
    EXPECT_EQ(G(result->Colors[0]), 0);
    EXPECT_EQ(B(result->Colors[0]), 0);

    // Second should be blue: R~0, G~0, B=255
    EXPECT_EQ(R(result->Colors[1]), 0);
    EXPECT_EQ(B(result->Colors[1]), 255);
}

TEST(ColorMapper, Vec4DirectRGBA)
{
    Geometry::PropertySet ps;
    auto prop = ps.Add<glm::vec4>("color", glm::vec4(0.0f));
    auto& vec = prop.Vector();
    vec.resize(1);
    vec[0] = glm::vec4(0.0f, 1.0f, 0.0f, 0.5f); // Green, half alpha

    ColorSource config;
    config.PropertyName = "color";

    auto result = ColorMapper::MapProperty(ps, config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Colors.size(), 1u);

    EXPECT_EQ(R(result->Colors[0]), 0);
    EXPECT_EQ(G(result->Colors[0]), 255);
    EXPECT_EQ(B(result->Colors[0]), 0);
    // Alpha should be ~128 (0.5 * 255)
    uint8_t a = static_cast<uint8_t>((result->Colors[0] >> 24) & 0xFF);
    EXPECT_NEAR(a, 128, 2);
}

TEST(ColorMapper, ScalarWithBinning)
{
    Geometry::PropertySet ps;
    auto prop = ps.Add<float>("val", 0.0f);
    auto& vec = prop.Vector();
    vec.resize(4);
    vec[0] = 0.1f;
    vec[1] = 0.2f;
    vec[2] = 0.8f;
    vec[3] = 0.9f;

    ColorSource config;
    config.PropertyName = "val";
    config.AutoRange = false;
    config.RangeMin = 0.0f;
    config.RangeMax = 1.0f;
    config.Bins = 2;
    config.Map = Colormap::Type::Viridis;

    auto result = ColorMapper::MapProperty(ps, config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Colors.size(), 4u);

    // First two should be the same (bin 0), last two should be the same (bin 1).
    EXPECT_EQ(result->Colors[0], result->Colors[1]);
    EXPECT_EQ(result->Colors[2], result->Colors[3]);
    EXPECT_NE(result->Colors[0], result->Colors[2]);
}

TEST(ColorMapper, SkipDeletedPredicate)
{
    Geometry::PropertySet ps;
    auto prop = ps.Add<float>("val", 0.0f);
    auto& vec = prop.Vector();
    vec.resize(4);
    vec[0] = 0.0f;
    vec[1] = 1.0f;
    vec[2] = 2.0f;
    vec[3] = 3.0f;

    ColorSource config;
    config.PropertyName = "val";
    config.AutoRange = true;
    config.Map = Colormap::Type::Viridis;

    // Skip element 1 and 3.
    auto result = ColorMapper::MapProperty(ps, config,
        [](size_t i) { return i == 1 || i == 3; });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Colors.size(), 2u);
}

TEST(ColorMapper, ConstantFieldAutoRange)
{
    Geometry::PropertySet ps;
    auto prop = ps.Add<float>("val", 0.0f);
    auto& vec = prop.Vector();
    vec.resize(3);
    vec[0] = 5.0f;
    vec[1] = 5.0f;
    vec[2] = 5.0f;

    ColorSource config;
    config.PropertyName = "val";
    config.AutoRange = true;
    config.Map = Colormap::Type::Viridis;

    auto result = ColorMapper::MapProperty(ps, config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Colors.size(), 3u);
    // Constant field: range expanded to [4.5, 5.5]
    EXPECT_FLOAT_EQ(config.RangeMin, 4.5f);
    EXPECT_FLOAT_EQ(config.RangeMax, 5.5f);
}
