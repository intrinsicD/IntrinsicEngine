// Bit-exact comparisons for property snapshots and stale-writeback guards.
// Integral values use equality; floating vectors compare component bits, not padding.
#pragma once
#include <bit>
#include <concepts>
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Extrinsic::Runtime::GeometryValueComparison
{
    template <std::integral T>
    [[nodiscard]] bool BitEqual(T lhs, T rhs) noexcept { return lhs == rhs; }

    [[nodiscard]] inline bool BitEqual(float lhs, float rhs) noexcept
    {
        return std::bit_cast<std::uint32_t>(lhs) == std::bit_cast<std::uint32_t>(rhs);
    }
    [[nodiscard]] inline bool BitEqual(double lhs, double rhs) noexcept
    {
        return std::bit_cast<std::uint64_t>(lhs) == std::bit_cast<std::uint64_t>(rhs);
    }
    [[nodiscard]] inline bool BitEqual(const glm::vec2& lhs, const glm::vec2& rhs) noexcept
    {
        return BitEqual(lhs.x, rhs.x) && BitEqual(lhs.y, rhs.y);
    }
    [[nodiscard]] inline bool BitEqual(const glm::vec3& lhs, const glm::vec3& rhs) noexcept
    {
        return BitEqual(lhs.x, rhs.x) && BitEqual(lhs.y, rhs.y) && BitEqual(lhs.z, rhs.z);
    }
    [[nodiscard]] inline bool BitEqual(const glm::vec4& lhs, const glm::vec4& rhs) noexcept
    {
        return BitEqual(lhs.x, rhs.x) && BitEqual(lhs.y, rhs.y) &&
               BitEqual(lhs.z, rhs.z) && BitEqual(lhs.w, rhs.w);
    }
}
