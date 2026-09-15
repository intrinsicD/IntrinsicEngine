// Pins exact snapshot comparison semantics independently of method eligibility.
#include <bit>
#include <cstdint>
#include <limits>
#include <gtest/gtest.h>
#include "../../../src/runtime/GeometryIntegration/Runtime.GeometryValueComparison.hpp"

namespace
{
    using Extrinsic::Runtime::GeometryValueComparison::BitEqual;

    TEST(GeometryValueComparison, ScalarBitsPreserveSignedZeroAndNanPayloads)
    {
        const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc00001});
        const auto otherNan = std::bit_cast<float>(std::uint32_t{0x7fc00002});
        EXPECT_TRUE(BitEqual(nan, nan));
        EXPECT_FALSE(BitEqual(nan, otherNan));
        EXPECT_FALSE(BitEqual(0.0f, -0.0f));
        EXPECT_TRUE(BitEqual(std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::infinity()));
        const auto nan64 = std::bit_cast<double>(std::uint64_t{0x7ff8000000000001});
        const auto other64 = std::bit_cast<double>(std::uint64_t{0x7ff8000000000002});
        EXPECT_TRUE(BitEqual(nan64, nan64));
        EXPECT_FALSE(BitEqual(nan64, other64));
        EXPECT_FALSE(BitEqual(0.0, -0.0));
        EXPECT_TRUE(BitEqual(true, true));
        EXPECT_FALSE(BitEqual(false, true));
        EXPECT_TRUE(BitEqual(std::int32_t{-1}, std::int32_t{-1}));
        EXPECT_FALSE(BitEqual(std::uint64_t{1} << 40, std::uint64_t{1} << 41));
    }

    TEST(GeometryValueComparison, VectorsCompareEveryComponentExactly)
    {
        const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc00001});
        const glm::vec4 value{nan, 0.0f, 2.0f, 3.0f};
        EXPECT_TRUE(BitEqual(value, value));
        EXPECT_TRUE(BitEqual(glm::vec3{value}, glm::vec3{value}));
        EXPECT_TRUE(BitEqual(glm::vec2{value}, glm::vec2{value}));
        for (int component = 0; component < 4; ++component)
        {
            auto changed = value;
            changed[component] = component == 1 ? -0.0f : 4.0f;
            EXPECT_FALSE(BitEqual(value, changed)) << component;
            if (component < 3)
                EXPECT_FALSE(BitEqual(glm::vec3{value}, glm::vec3{changed})) << component;
            if (component < 2)
                EXPECT_FALSE(BitEqual(glm::vec2{value}, glm::vec2{changed})) << component;
        }
    }
}
