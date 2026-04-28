#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace Graphics::Detail
{
    // Private helpers for runtime-side asset/file-format plumbing.
    [[nodiscard]] inline std::string ToLowerAscii(std::string_view value)
    {
        std::string lowered(value);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lowered;
    }

    [[nodiscard]] inline std::string LowercaseExtension(const std::filesystem::path& path)
    {
        return ToLowerAscii(path.extension().string());
    }

    // Accept both normalized [0, 1] channels and byte-style [0, 255] channels.
    [[nodiscard]] inline float NormalizeColorChannelToUnitRange(float value)
    {
        return value > 1.0f ? (value / 255.0f) : value;
    }
}

