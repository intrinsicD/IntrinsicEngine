module;

#include <cstdint>
#include <string_view>

export module Graphics:AssetErrors;

export namespace Graphics
{
    enum class AssetError : std::uint32_t
    {
        FileNotFound,
        DecodeFailed,
        UnsupportedFormat,
        UploadFailed,
        InvalidData
    };

    [[nodiscard]] constexpr std::string_view AssetErrorToString(AssetError e) noexcept
    {
        switch (e)
        {
            case AssetError::FileNotFound:       return "FileNotFound";
            case AssetError::DecodeFailed:       return "DecodeFailed";
            case AssetError::UnsupportedFormat:  return "UnsupportedFormat";
            case AssetError::UploadFailed:       return "UploadFailed";
            case AssetError::InvalidData:        return "InvalidData";
            default:                             return "Unknown";
        }
    }
}

