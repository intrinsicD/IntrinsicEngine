// Pins exact snapshot comparison semantics independently of method eligibility.
#include <bit>
#include <cstdint>
#include <limits>
#include <vector>
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

    TEST(GeometryValueComparison, BuffersPreserveSizeAndFloatingPointBits)
    {
        const auto check = []<typename T>(const T value, const T different)
        {
            const std::vector<T> empty;
            EXPECT_TRUE(BitEqual(empty, empty));
            const std::vector<T> lhs{value, value};
            auto rhs = lhs;
            EXPECT_TRUE(BitEqual(lhs, rhs));
            EXPECT_FALSE(BitEqual(lhs, empty));
            EXPECT_FALSE(BitEqual(empty, lhs));
            rhs.back() = different;
            EXPECT_FALSE(BitEqual(lhs, rhs));
            rhs = lhs;
            rhs.pop_back();
            EXPECT_FALSE(BitEqual(lhs, rhs));
        };

        const auto nan = std::bit_cast<float>(std::uint32_t{0x7fc00001});
        const auto otherNan = std::bit_cast<float>(std::uint32_t{0x7fc00002});
        check(nan, otherNan);
        check(0.0f, -0.0f);
        check(std::bit_cast<double>(std::uint64_t{0x7ff8000000000001}),
              std::bit_cast<double>(std::uint64_t{0x7ff8000000000002}));
        check(0.0, -0.0);
        check(glm::vec2{nan, 0.0f}, glm::vec2{otherNan, 0.0f});
        check(glm::vec3{nan, 1.0f, 0.0f}, glm::vec3{nan, 1.0f, -0.0f});
        check(glm::vec4{nan, 1.0f, 2.0f, 0.0f},
              glm::vec4{nan, 1.0f, 2.0f, -0.0f});
    }

    TEST(GeometryValueComparison, PackedBooleanBuffersCompareEveryStoredValue)
    {
        const std::vector<bool> lhs(65u, true);
        auto rhs = lhs;
        EXPECT_TRUE(BitEqual(lhs, rhs));
        rhs.back() = false;
        EXPECT_FALSE(BitEqual(lhs, rhs));
        rhs = lhs;
        rhs.front() = false;
        EXPECT_FALSE(BitEqual(lhs, rhs));
        rhs = lhs;
        rhs.pop_back();
        EXPECT_FALSE(BitEqual(lhs, rhs));
    }

    TEST(GeometryValueComparison, IntegralBuffersPreserveWideAndSignedValues)
    {
        const std::vector<std::uint64_t> lhs{std::uint64_t{1} << 60};
        auto rhs = lhs;
        EXPECT_TRUE(BitEqual(lhs, rhs));
        ++rhs.front();
        EXPECT_FALSE(BitEqual(lhs, rhs));
        const std::vector<std::int32_t> negative{-1, -2};
        auto changed = negative;
        EXPECT_TRUE(BitEqual(negative, changed));
        changed.back() = 2;
        EXPECT_FALSE(BitEqual(negative, changed));
    }
}
