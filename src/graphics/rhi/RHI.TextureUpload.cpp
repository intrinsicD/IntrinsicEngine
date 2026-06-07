module;

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

module Extrinsic.RHI.TextureUpload;

namespace Extrinsic::RHI
{
    Core::Expected<TextureUploadLayout> ComputeFullChainUploadLayout(const TextureDesc& desc) noexcept
    {
        if (desc.Width == 0u || desc.Height == 0u || desc.MipLevels == 0u
            || desc.DepthOrArrayLayers == 0u)
        {
            return Core::Err<TextureUploadLayout>(Core::ErrorCode::InvalidArgument);
        }
        if (!IsUploadableFormat(desc.Fmt))
        {
            return Core::Err<TextureUploadLayout>(Core::ErrorCode::InvalidFormat);
        }

        const bool is3D = (desc.Dimension == TextureDimension::Tex3D);
        const std::uint32_t arrayLayers = is3D ? 1u : desc.DepthOrArrayLayers;
        const std::uint32_t alignment = RequiredBufferOffsetAlignment(desc.Fmt);

        TextureUploadLayout layout{};
        layout.Subresources.reserve(static_cast<std::size_t>(arrayLayers) * desc.MipLevels);

        std::uint64_t cursor = 0u;
        for (std::uint32_t layer = 0u; layer < arrayLayers; ++layer)
        {
            for (std::uint32_t mip = 0u; mip < desc.MipLevels; ++mip)
            {
                const std::uint64_t bytes = ComputeSubresourceUploadSize(desc, mip);
                if (bytes == 0u)
                {
                    return Core::Err<TextureUploadLayout>(Core::ErrorCode::InvalidArgument);
                }

                // Alignment may be 12 (RGB32_FLOAT), so bitmask rounding is unsafe here.
                cursor = ((cursor + alignment - 1u) / alignment) * alignment;

                TextureUploadSubresource sub{};
                sub.MipLevel = mip;
                sub.ArrayLayer = layer;
                sub.Width = MipExtent(desc.Width, mip);
                sub.Height = MipExtent(desc.Height, mip);
                sub.Depth = is3D ? MipExtent(desc.DepthOrArrayLayers, mip) : 1u;
                sub.OffsetBytes = cursor;
                sub.SizeBytes = bytes;
                layout.Subresources.push_back(sub);

                cursor += bytes;
            }
        }
        layout.TotalBytes = cursor;
        return Core::Ok(std::move(layout));
    }
}
