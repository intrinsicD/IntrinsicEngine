#include <gtest/gtest.h>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>
#include <optional>

#include <glm/glm.hpp>

import Graphics;
import Geometry;

using namespace Graphics;

// =============================================================================
// ABGR channel extraction helpers (shared by Colormap and ColorMapper tests).
// =============================================================================

static uint8_t R(uint32_t c) { return static_cast<uint8_t>(c & 0xFF); }
static uint8_t G(uint32_t c) { return static_cast<uint8_t>((c >> 8) & 0xFF); }
static uint8_t B(uint32_t c) { return static_cast<uint8_t>((c >> 16) & 0xFF); }
static uint8_t A(uint32_t c) { return static_cast<uint8_t>((c >> 24) & 0xFF); }

// =============================================================================
// Colormap — unit tests for scientific colormaps.
// =============================================================================

TEST(Colormap, AllMapsReturnFullAlpha)
{
    for (uint32_t m = 0; m < Colormap::Count; ++m)
    {
        auto map = static_cast<Colormap::Type>(m);
        for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        {
            uint32_t c = Colormap::Sample(map, t);
            EXPECT_EQ(A(c), 255) << "Map " << Colormap::Name(map) << " t=" << t;
        }
    }
}

TEST(Colormap, ClampsOutOfRange)
{
    auto c_neg = Colormap::Sample(Colormap::Type::Viridis, -1.0f);
    auto c_zero = Colormap::Sample(Colormap::Type::Viridis, 0.0f);
    EXPECT_EQ(c_neg, c_zero);

    auto c_over = Colormap::Sample(Colormap::Type::Viridis, 2.0f);
    auto c_one = Colormap::Sample(Colormap::Type::Viridis, 1.0f);
    EXPECT_EQ(c_over, c_one);
}

TEST(Colormap, ViridisEndpoints)
{
    // Viridis starts dark purple, ends bright yellow.
    auto c0 = Colormap::Sample(Colormap::Type::Viridis, 0.0f);
    auto c1 = Colormap::Sample(Colormap::Type::Viridis, 1.0f);

    // t=0: dark, t=1: bright
    float lum0 = 0.299f * R(c0) + 0.587f * G(c0) + 0.114f * B(c0);
    float lum1 = 0.299f * R(c1) + 0.587f * G(c1) + 0.114f * B(c1);
    EXPECT_LT(lum0, lum1) << "Viridis should go dark to bright";
}

TEST(Colormap, JetIsCyanAtQuarter)
{
    // Jet at t=0.375 should be near cyan (G and B high, R low).
    auto c = Colormap::Sample(Colormap::Type::Jet, 0.375f);
    EXPECT_LT(R(c), 50);
    EXPECT_GT(G(c), 150);
    EXPECT_GT(B(c), 150);
}

TEST(Colormap, CoolwarmDivergingCenter)
{
    // Coolwarm at t=0.5 should be near white/grey.
    auto c = Colormap::Sample(Colormap::Type::Coolwarm, 0.5f);
    // All channels should be close to each other and bright.
    EXPECT_GT(R(c), 180);
    EXPECT_GT(G(c), 180);
    EXPECT_GT(B(c), 180);
}

TEST(Colormap, SampleBinnedQuantizes)
{
    // With 2 bins, any t in [0, 0.5) should map to the same color,
    // and any t in [0.5, 1.0] to another.
    auto c_low1 = Colormap::SampleBinned(Colormap::Type::Viridis, 0.1f, 2);
    auto c_low2 = Colormap::SampleBinned(Colormap::Type::Viridis, 0.4f, 2);
    EXPECT_EQ(c_low1, c_low2);

    auto c_hi1 = Colormap::SampleBinned(Colormap::Type::Viridis, 0.6f, 2);
    auto c_hi2 = Colormap::SampleBinned(Colormap::Type::Viridis, 0.9f, 2);
    EXPECT_EQ(c_hi1, c_hi2);

    EXPECT_NE(c_low1, c_hi1);
}

TEST(Colormap, SampleBinnedZeroBinsIsContinuous)
{
    auto c_binned = Colormap::SampleBinned(Colormap::Type::Inferno, 0.33f, 0);
    auto c_cont = Colormap::Sample(Colormap::Type::Inferno, 0.33f);
    EXPECT_EQ(c_binned, c_cont);
}

TEST(Colormap, NameReturnsNonNull)
{
    for (uint32_t m = 0; m < Colormap::Count; ++m)
    {
        auto map = static_cast<Colormap::Type>(m);
        const char* name = Colormap::Name(map);
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::strlen(name), 0u);
    }
}

TEST(Colormap, HeatEndpoints)
{
    // Heat at t=0 should be blue (B high, R/G low).
    auto c0 = Colormap::Sample(Colormap::Type::Heat, 0.0f);
    EXPECT_GT(B(c0), 200);
    EXPECT_LT(R(c0), 50);

    // Heat at t=1 should be red (R high, G/B low).
    auto c1 = Colormap::Sample(Colormap::Type::Heat, 1.0f);
    EXPECT_GT(R(c1), 200);
    EXPECT_LT(G(c1), 50);
}

// =============================================================================
// ColorMapper — unit tests for PropertySet -> packed ABGR mapping.
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

// =============================================================================
// ColorMapper — double property support (geometry processing results)
// =============================================================================

TEST(ColorMapper, DoubleScalarAutoRange)
{
    // Geometry processing algorithms (geodesic, shortest path, curvature) store
    // results as double. The color mapper must handle them transparently.
    Geometry::PropertySet ps;
    auto prop = ps.Add<double>("v:geodesic_distance", 0.0);
    auto& vec = prop.Vector();
    vec.resize(4);
    vec[0] = 0.0;
    vec[1] = 1.0;
    vec[2] = 2.0;
    vec[3] = 3.0;

    ColorSource config;
    config.PropertyName = "v:geodesic_distance";
    config.AutoRange = true;
    config.Map = Colormap::Type::Viridis;

    auto result = ColorMapper::MapProperty(ps, config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Colors.size(), 4u);
    EXPECT_FLOAT_EQ(config.RangeMin, 0.0f);
    EXPECT_FLOAT_EQ(config.RangeMax, 3.0f);
}

TEST(ColorMapper, DoubleScalarMapsToDistinctColors)
{
    Geometry::PropertySet ps;
    auto prop = ps.Add<double>("v:shortest_path_distance", 0.0);
    auto& vec = prop.Vector();
    vec.resize(3);
    vec[0] = 0.0;
    vec[1] = 0.5;
    vec[2] = 1.0;

    ColorSource config;
    config.PropertyName = "v:shortest_path_distance";
    config.AutoRange = false;
    config.RangeMin = 0.0f;
    config.RangeMax = 1.0f;
    config.Map = Colormap::Type::Viridis;

    auto result = ColorMapper::MapProperty(ps, config);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->Colors.size(), 3u);

    // All three values should map to different colors.
    EXPECT_NE(result->Colors[0], result->Colors[1]);
    EXPECT_NE(result->Colors[1], result->Colors[2]);
}

TEST(ColorMapper, DoubleInfinityIgnoredInAutoRange)
{
    // Dijkstra initialises unreachable vertices to +infinity.
    // AutoRange must skip infinities and clamp to the finite range.
    Geometry::PropertySet ps;
    auto prop = ps.Add<double>("v:shortest_path_distance",
                               std::numeric_limits<double>::infinity());
    auto& vec = prop.Vector();
    vec.resize(4);
    vec[0] = 0.0;
    vec[1] = 1.0;
    vec[2] = std::numeric_limits<double>::infinity(); // unreachable vertex
    vec[3] = 2.0;

    ColorSource config;
    config.PropertyName = "v:shortest_path_distance";
    config.AutoRange = true;
    config.Map = Colormap::Type::Viridis;

    auto result = ColorMapper::MapProperty(ps, config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Colors.size(), 4u);
    // AutoRange should use [0, 2], not [0, inf].
    EXPECT_FLOAT_EQ(config.RangeMin, 0.0f);
    EXPECT_FLOAT_EQ(config.RangeMax, 2.0f);
}

// =============================================================================
// PropertyEnumerator — double property discovery
// =============================================================================

TEST(PropertyEnumerator, DoublePropertyAppearsAsScalar)
{
    Geometry::PropertySet ps;
    ps.Add<double>("v:geodesic_distance", 0.0);
    ps.Add<double>("v:mean_curvature", 0.0);
    ps.Add<float>("v:some_float", 0.0f);
    ps.Add<glm::vec3>("v:normal", glm::vec3(0.0f));

    const Geometry::ConstPropertySet cps{ps};
    const auto infos = Graphics::EnumerateColorableProperties(cps);

    // All three colorable properties must be discovered.
    EXPECT_EQ(infos.size(), 3u);

    auto findProp = [&](const std::string& name) {
        for (const auto& info : infos)
            if (info.Name == name) return true;
        return false;
    };
    EXPECT_TRUE(findProp("v:geodesic_distance"));
    EXPECT_TRUE(findProp("v:mean_curvature"));
    EXPECT_TRUE(findProp("v:some_float"));
}

TEST(PropertyEnumerator, DoublePropertyAppearsInScalarEnumeration)
{
    Geometry::PropertySet ps;
    ps.Add<double>("v:transported_angle", 0.0);
    ps.Add<float>("v:curvature_f", 0.0f);

    const Geometry::ConstPropertySet cps{ps};
    const auto scalars = Graphics::EnumerateScalarProperties(cps);

    EXPECT_EQ(scalars.size(), 2u);
    for (const auto& info : scalars)
        EXPECT_EQ(info.Type, Graphics::PropertyDataType::Scalar);
}
