// Contract tests for the reusable vertex attribute binding resolver
// (RUNTIME-120). CPU-only; no GPU/RHI dependency.

#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

import Extrinsic.Runtime.VertexAttributeBinding;
import Geometry.Properties;

using Extrinsic::Runtime::AttributeBindStatus;
using Extrinsic::Runtime::AttributeSourceType;
using Extrinsic::Runtime::ResolveColorChannelPackedUnorm8;
using Extrinsic::Runtime::ResolveVec2Channel;
using Extrinsic::Runtime::ResolveVec3Channel;
using Extrinsic::Runtime::VertexAttributeBinding;
using Extrinsic::Runtime::VertexChannel;

namespace
{
    constexpr float kInf = std::numeric_limits<float>::infinity();
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

    VertexAttributeBinding NormalBinding(std::string_view name)
    {
        return VertexAttributeBinding{
            .Channel = VertexChannel::Normal,
            .SourceType = AttributeSourceType::Vec3,
            .SourceProperty = name,
            .AllowFallback = true,
            .Normalize = true,
            .Fallback = glm::vec4{0.0f, 0.0f, 1.0f, 0.0f},
        };
    }
} // namespace

TEST(VertexAttributeBinding, MissingPropertyFallsBackWhenAllowed)
{
    Geometry::PropertySet properties;
    properties.Resize(3);

    std::vector<glm::vec3> out(3);
    const auto result = ResolveVec3Channel(properties, NormalBinding("v:normal"), 3, out);

    EXPECT_EQ(result.Status, AttributeBindStatus::PropertyMissing);
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.FallbackCount, 3u);
    EXPECT_EQ(result.SourceCount, 0u);
    for (const auto& n : out)
    {
        EXPECT_EQ(n, (glm::vec3{0.0f, 0.0f, 1.0f}));
    }
}

TEST(VertexAttributeBinding, MissingPropertyNotPopulatedWhenFallbackDisabled)
{
    Geometry::PropertySet properties;
    properties.Resize(2);

    auto binding = NormalBinding("v:normal");
    binding.AllowFallback = false;

    std::vector<glm::vec3> out{glm::vec3{9.0f}, glm::vec3{9.0f}};
    const auto result = ResolveVec3Channel(properties, binding, 2, out);

    EXPECT_EQ(result.Status, AttributeBindStatus::PropertyMissing);
    EXPECT_FALSE(result.Ok());
    EXPECT_EQ(result.FallbackCount, 0u);
    // Output left untouched.
    EXPECT_EQ(out[0], glm::vec3{9.0f});
}

TEST(VertexAttributeBinding, TypeMismatchIsDistinctFromMissing)
{
    Geometry::PropertySet properties;
    properties.Resize(2);
    // Present under the requested name, but as vec2 not vec3.
    auto uv = properties.Add<glm::vec2>("v:normal", glm::vec2{0.0f});
    ASSERT_TRUE(uv);

    std::vector<glm::vec3> out(2);
    const auto result = ResolveVec3Channel(properties, NormalBinding("v:normal"), 2, out);

    EXPECT_EQ(result.Status, AttributeBindStatus::TypeMismatch);
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.FallbackCount, 2u);
}

TEST(VertexAttributeBinding, CountMismatchFallsBack)
{
    Geometry::PropertySet properties;
    properties.Resize(4);
    auto normal = properties.Add<glm::vec3>("v:normal", glm::vec3{1.0f, 0.0f, 0.0f});
    ASSERT_TRUE(normal);

    std::vector<glm::vec3> out(3);
    const auto result = ResolveVec3Channel(properties, NormalBinding("v:normal"), 3, out);

    EXPECT_EQ(result.Status, AttributeBindStatus::CountMismatch);
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.FallbackCount, 3u);
}

TEST(VertexAttributeBinding, EmptyBindingFallsBack)
{
    Geometry::PropertySet properties;
    properties.Resize(2);

    std::vector<glm::vec3> out(2);
    const auto result = ResolveVec3Channel(properties, NormalBinding({}), 2, out);

    EXPECT_EQ(result.Status, AttributeBindStatus::EmptyBinding);
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.FallbackCount, 2u);
}

TEST(VertexAttributeBinding, Vec3RenormalizesAndRepairsDegenerateAndNonFinite)
{
    Geometry::PropertySet properties;
    properties.Resize(3);
    auto normal = properties.Add<glm::vec3>("v:normal", glm::vec3{0.0f});
    ASSERT_TRUE(normal);
    normal.Vector()[0] = glm::vec3{0.0f, 0.0f, 5.0f};  // finite, renormalizes to +Z
    normal.Vector()[1] = glm::vec3{0.0f, 0.0f, 0.0f};  // degenerate -> fallback
    normal.Vector()[2] = glm::vec3{kNaN, 0.0f, 0.0f};  // non-finite -> fallback

    std::vector<glm::vec3> out(3);
    const auto result = ResolveVec3Channel(properties, NormalBinding("v:normal"), 3, out);

    EXPECT_EQ(result.Status, AttributeBindStatus::Bound);
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.SourceCount, 1u);
    EXPECT_EQ(result.FallbackCount, 2u);
    EXPECT_EQ(result.NonFiniteCount, 1u);
    EXPECT_FLOAT_EQ(glm::length(out[0]), 1.0f);
    EXPECT_EQ(out[0], (glm::vec3{0.0f, 0.0f, 1.0f}));
    EXPECT_EQ(out[1], (glm::vec3{0.0f, 0.0f, 1.0f}));
    EXPECT_EQ(out[2], (glm::vec3{0.0f, 0.0f, 1.0f}));
}

TEST(VertexAttributeBinding, Vec2TexcoordRepairsNonFinitePerElement)
{
    Geometry::PropertySet properties;
    properties.Resize(2);
    auto uv = properties.Add<glm::vec2>("v:texcoord", glm::vec2{0.0f});
    ASSERT_TRUE(uv);
    uv.Vector()[0] = glm::vec2{0.25f, 0.75f};
    uv.Vector()[1] = glm::vec2{kInf, 0.0f};

    const VertexAttributeBinding binding{
        .Channel = VertexChannel::Texcoord,
        .SourceType = AttributeSourceType::Vec2,
        .SourceProperty = "v:texcoord",
        .AllowFallback = true,
        .Normalize = false,
        .Fallback = glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
    };

    std::vector<glm::vec2> out(2);
    const auto result = ResolveVec2Channel(properties, binding, 2, out);

    EXPECT_EQ(result.Status, AttributeBindStatus::Bound);
    EXPECT_EQ(result.SourceCount, 1u);
    EXPECT_EQ(result.FallbackCount, 1u);
    EXPECT_EQ(out[0], (glm::vec2{0.25f, 0.75f}));
    EXPECT_EQ(out[1], (glm::vec2{0.0f, 0.0f}));
}

TEST(VertexAttributeBinding, ColorVec4PacksUnorm8RoundTrip)
{
    Geometry::PropertySet properties;
    properties.Resize(2);
    auto color = properties.Add<glm::vec4>("v:color", glm::vec4{0.0f});
    ASSERT_TRUE(color);
    color.Vector()[0] = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};   // opaque red
    color.Vector()[1] = glm::vec4{0.0f, 1.0f, 0.0f, 0.5f};   // half-alpha green

    const VertexAttributeBinding binding{
        .Channel = VertexChannel::Color,
        .SourceType = AttributeSourceType::Vec4,
        .SourceProperty = "v:color",
        .AllowFallback = true,
        .Normalize = false,
        .Fallback = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f},
    };

    std::vector<std::uint32_t> out(2);
    const auto result = ResolveColorChannelPackedUnorm8(properties, binding, 2, out);

    EXPECT_EQ(result.Status, AttributeBindStatus::Bound);
    EXPECT_EQ(result.SourceCount, 2u);
    // unpackUnorm4x8 maps byte0->R, byte1->G, byte2->B, byte3->A.
    EXPECT_EQ(out[0] & 0xffu, 255u);              // R
    EXPECT_EQ((out[0] >> 24) & 0xffu, 255u);      // A
    EXPECT_EQ((out[1] >> 8) & 0xffu, 255u);       // G
    EXPECT_EQ((out[1] >> 24) & 0xffu, 128u);      // A ~ 0.5
}

TEST(VertexAttributeBinding, ColorVec3PacksOpaqueAlpha)
{
    Geometry::PropertySet properties;
    properties.Resize(1);
    auto color = properties.Add<glm::vec3>("v:color", glm::vec3{0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(color);

    const VertexAttributeBinding binding{
        .Channel = VertexChannel::Color,
        .SourceType = AttributeSourceType::Vec3,
        .SourceProperty = "v:color",
        .AllowFallback = true,
        .Normalize = false,
        .Fallback = glm::vec4{0.0f, 0.0f, 0.0f, 1.0f},
    };

    std::vector<std::uint32_t> out(1);
    const auto result = ResolveColorChannelPackedUnorm8(properties, binding, 1, out);

    EXPECT_EQ(result.Status, AttributeBindStatus::Bound);
    EXPECT_EQ((out[0] >> 16) & 0xffu, 255u);  // B
    EXPECT_EQ((out[0] >> 24) & 0xffu, 255u);  // opaque A
}
