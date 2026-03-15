// Graphics.Importers.ColorParsing.hpp — Shared color parsing helpers for
// importers that read per-vertex/per-point color data from text tokens.
// Consolidates color triplet parsing, intensity-as-grayscale conversion,
// and packed-RGB unpacking previously duplicated across XYZ, PCD, and OFF.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <glm/glm.hpp>

#include "Graphics.FileFormatUtils.hpp"
#include "Graphics.Importers.TextParse.hpp"

namespace Graphics::Importers
{
    // Parse an RGB triplet from consecutive text tokens at the given offset.
    // Auto-detects [0,255] vs [0,1] range via NormalizeColorChannelToUnitRange.
    // Returns nullopt if fewer than 3 parseable tokens remain at offset.
    [[nodiscard]] inline std::optional<glm::vec4> ParseRgbTriplet(
        std::span<const std::string_view> tokens,
        std::size_t offset)
    {
        if (offset + 2 >= tokens.size())
            return std::nullopt;

        const auto r = TextParse::ParseNumber<float>(tokens[offset + 0]);
        const auto g = TextParse::ParseNumber<float>(tokens[offset + 1]);
        const auto b = TextParse::ParseNumber<float>(tokens[offset + 2]);
        if (!r || !g || !b)
            return std::nullopt;

        return glm::vec4(
            Detail::NormalizeColorChannelToUnitRange(*r),
            Detail::NormalizeColorChannelToUnitRange(*g),
            Detail::NormalizeColorChannelToUnitRange(*b),
            1.0f);
    }

    // Convert a single intensity/luminance value to a grayscale RGBA color.
    // Auto-detects [0,255] vs [0,1] range.
    [[nodiscard]] inline glm::vec4 IntensityToColor(float intensity)
    {
        const float v = Detail::NormalizeColorChannelToUnitRange(intensity);
        return glm::vec4(v, v, v, 1.0f);
    }

    // Unpack a 32-bit packed RGB value (0x00RRGGBB) into normalized [0,1] RGBA.
    [[nodiscard]] inline glm::vec4 UnpackPackedRgb(std::uint32_t packed)
    {
        return glm::vec4(
            static_cast<float>((packed >> 16) & 0xFFu) / 255.0f,
            static_cast<float>((packed >> 8) & 0xFFu) / 255.0f,
            static_cast<float>(packed & 0xFFu) / 255.0f,
            1.0f);
    }
}
