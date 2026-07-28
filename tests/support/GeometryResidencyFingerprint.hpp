#pragma once

#include <bit>
#include <cstdint>
#include <initializer_list>

namespace Extrinsic::Tests
{
    inline constexpr std::uint64_t kGeometryFingerprintOffset =
        14695981039346656037ull;
    inline constexpr std::uint64_t kGeometryFingerprintPrime =
        1099511628211ull;

    inline void AppendGeometryFingerprintWord(
        std::uint64_t& fingerprint,
        const std::uint32_t value) noexcept
    {
        for (std::uint32_t shift = 0u; shift < 32u; shift += 8u)
        {
            fingerprint ^= static_cast<std::uint8_t>(value >> shift);
            fingerprint *= kGeometryFingerprintPrime;
        }
    }

    [[nodiscard]] inline std::uint64_t GeometryUint32Fingerprint(
        const std::initializer_list<std::uint32_t> values) noexcept
    {
        std::uint64_t fingerprint = kGeometryFingerprintOffset;
        for (const std::uint32_t value : values)
            AppendGeometryFingerprintWord(fingerprint, value);
        return fingerprint == 0u ? 1u : fingerprint;
    }

    [[nodiscard]] inline std::uint64_t GeometryFloat32Fingerprint(
        const std::initializer_list<float> values) noexcept
    {
        std::uint64_t fingerprint = kGeometryFingerprintOffset;
        for (const float value : values)
        {
            std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
            if (bits == 0x8000'0000u)
                bits = 0u;
            AppendGeometryFingerprintWord(fingerprint, bits);
        }
        return fingerprint == 0u ? 1u : fingerprint;
    }
}
