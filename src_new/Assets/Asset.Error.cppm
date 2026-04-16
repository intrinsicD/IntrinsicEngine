module;

#include <cstdint>
#include <string_view>

export module Extrinsic.Asset.Error;

import Extrinsic.Core.Error;

namespace Extrinsic::Assets::Error
{
    export enum class AssetError : std::uint32_t
    {
        FileNotFound = Core::Error::ToUnderlying(Core::ErrorCode::FileNotFound),
        DecodeFailed = Core::Error::ToUnderlying(Core::ErrorCode::AssetDecodeFailed),
        UnsupportedFormat = Core::Error::ToUnderlying(Core::ErrorCode::AssetUnsupportedFormat),
        UploadFailed = Core::Error::ToUnderlying(Core::ErrorCode::AssetUploadFailed),
        InvalidData = Core::Error::ToUnderlying(Core::ErrorCode::AssetInvalidData),
    };

    [[nodiscard]] constexpr std::string_view ToString(AssetError e) noexcept
    {
        switch (e)
        {
        case AssetError::FileNotFound: return "FileNotFound";
        case AssetError::DecodeFailed: return "DecodeFailed";
        case AssetError::UnsupportedFormat: return "UnsupportedFormat";
        case AssetError::UploadFailed: return "UploadFailed";
        case AssetError::InvalidData: return "InvalidData";
        default: return "Unknown";
        }
    }
}
