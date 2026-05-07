module;

#include <cstdint>
#include <vector>

export module Extrinsic.RHI.TextureUpload;

import Extrinsic.Core.Error;
import Extrinsic.RHI.Descriptors;

// ============================================================
// RHI.TextureUpload — CPU-testable byte-size and packed-layout
// math for multi-subresource texture uploads.
//
// Owned by GRAPHICS-018T. The Vulkan backend currently issues one
// blocking VkBufferImageCopy per (mipLevel, arrayLayer); the math
// for "how many bytes does this subresource need?" lives only in
// `src/graphics/vulkan/Backends.Vulkan.Device.cpp` as anonymous-
// namespace helpers keyed off VkFormat. That makes the contract
// untestable from CPU contract suites and forces every backend to
// re-derive the same packing rules.
//
// This module exposes the math as backend-neutral free functions
// keyed off `RHI::Format` plus a packed `TextureUploadLayout`
// describing how a contiguous host staging buffer maps onto the
// (subresource, byte-range) tuples a backend must submit. The
// layout convention is layer-major / mip-minor: for each array
// layer L in [0, ArrayLayers), iterate mip M in [0, MipLevels).
// Matches GRAPHICS-018Q's whole-image layout policy: a backend
// uploads every subresource through one staging buffer and one
// barrier-bracket pair, never tracking per-subresource layouts.
//
// Slice A.1 is purely additive. Slice A.2 will rewrite the Vulkan
// `UploadTexture()` path to consume this layout in a single
// `VkBufferImageCopy`-per-subresource batch.
// ============================================================

export namespace Extrinsic::RHI
{
    // ----------------------------------------------------------
    // Format-level helpers (CPU-pure, no backend dependency)
    // ----------------------------------------------------------

    /// Returns true for block-compressed formats (BC1/BC3/BC5/BC7*).
    /// Block-compressed uploads pack 4x4 texel tiles into a single
    /// block worth of bytes; subresource dimensions still advertise
    /// texel extents but byte counts round up to whole blocks.
    [[nodiscard]] constexpr bool IsBlockCompressedFormat(Format fmt) noexcept
    {
        switch (fmt)
        {
        case Format::BC1_UNORM:
        case Format::BC3_UNORM:
        case Format::BC5_UNORM:
        case Format::BC7_UNORM:
        case Format::BC7_SRGB:
            return true;
        default:
            return false;
        }
    }

    /// Returns true for depth-stencil formats. The current upload
    /// contract rejects depth-stencil because separate-aspect
    /// staging+copy semantics are not yet supported by the
    /// fail-closed `WriteTexture()` path or `UploadTexture()`.
    [[nodiscard]] constexpr bool IsDepthStencilFormat(Format fmt) noexcept
    {
        switch (fmt)
        {
        case Format::D16_UNORM:
        case Format::D32_FLOAT:
        case Format::D24_UNORM_S8_UINT:
        case Format::D32_FLOAT_S8_UINT:
            return true;
        default:
            return false;
        }
    }

    /// Texel-block extent: 4 for BC* formats, 1 otherwise.
    /// Width/height are rounded up to the next multiple of this
    /// value before block-byte multiplication.
    [[nodiscard]] constexpr std::uint32_t BlockExtent(Format fmt) noexcept
    {
        return IsBlockCompressedFormat(fmt) ? 4u : 1u;
    }

    /// Bytes per texel-block. Returns 0 for `Format::Undefined`,
    /// for depth-stencil formats (which have no flat byte form
    /// suitable for the current single-aspect upload path), and
    /// for any format the upload contract does not support.
    [[nodiscard]] constexpr std::uint32_t BytesPerBlock(Format fmt) noexcept
    {
        switch (fmt)
        {
        case Format::R8_UNORM:           return 1;
        case Format::RG8_UNORM:          return 2;
        case Format::RGBA8_UNORM:        return 4;
        case Format::RGBA8_SRGB:         return 4;
        case Format::BGRA8_UNORM:        return 4;
        case Format::BGRA8_SRGB:         return 4;
        case Format::R16_FLOAT:          return 2;
        case Format::R16_UINT:           return 2;
        case Format::R16_UNORM:          return 2;
        case Format::RG16_FLOAT:         return 4;
        case Format::RGBA16_FLOAT:       return 8;
        case Format::R32_FLOAT:          return 4;
        case Format::R32_UINT:           return 4;
        case Format::R32_SINT:           return 4;
        case Format::RG32_FLOAT:         return 8;
        case Format::RGB32_FLOAT:        return 12;
        case Format::RGBA32_FLOAT:       return 16;
        case Format::BC1_UNORM:          return 8;
        case Format::BC3_UNORM:          return 16;
        case Format::BC5_UNORM:          return 16;
        case Format::BC7_UNORM:          return 16;
        case Format::BC7_SRGB:           return 16;
        // Depth-stencil and undefined formats are not part of the
        // current single-aspect upload contract.
        case Format::D16_UNORM:
        case Format::D32_FLOAT:
        case Format::D24_UNORM_S8_UINT:
        case Format::D32_FLOAT_S8_UINT:
        case Format::Undefined:
            return 0;
        }
        return 0;
    }

    /// Returns true when the format has a defined contiguous host
    /// upload representation through `WriteTexture()` /
    /// `ITransferQueue::UploadTexture()`.
    [[nodiscard]] constexpr bool IsUploadableFormat(Format fmt) noexcept
    {
        return BytesPerBlock(fmt) != 0u;
    }

    // ----------------------------------------------------------
    // Mip extent helpers
    // ----------------------------------------------------------

    /// Per-mip texel extent. Mip 0 keeps the base extent; each
    /// subsequent mip halves and clamps to a minimum of 1.
    [[nodiscard]] constexpr std::uint32_t MipExtent(std::uint32_t extent,
                                                    std::uint32_t mipLevel) noexcept
    {
        const std::uint32_t shifted = extent >> mipLevel;
        return shifted == 0u ? 1u : shifted;
    }

    // ----------------------------------------------------------
    // Per-subresource byte-size math
    // ----------------------------------------------------------

    /// Required upload byte count for a single subresource of `desc`
    /// at `mipLevel`. Returns 0 when the format is unsupported (see
    /// `IsUploadableFormat`), the mip level is out of range, or the
    /// mip extent computes to zero blocks.
    ///
    /// For block-compressed formats the byte count rounds the per-mip
    /// width and height up to the block extent before multiplying by
    /// the block size; depth always stays 1 for the 2D / cube paths
    /// the current contract supports.
    [[nodiscard]] constexpr std::uint64_t ComputeSubresourceUploadSize(
        const TextureDesc& desc, std::uint32_t mipLevel) noexcept
    {
        const std::uint32_t blockBytes = BytesPerBlock(desc.Fmt);
        if (blockBytes == 0u)
            return 0u;
        if (mipLevel >= desc.MipLevels)
            return 0u;

        const std::uint32_t width  = MipExtent(desc.Width,  mipLevel);
        const std::uint32_t height = MipExtent(desc.Height, mipLevel);
        const std::uint32_t depth  = (desc.Dimension == TextureDimension::Tex3D)
                                         ? MipExtent(desc.DepthOrArrayLayers, mipLevel)
                                         : 1u;

        if (IsBlockCompressedFormat(desc.Fmt))
        {
            const std::uint32_t bx = BlockExtent(desc.Fmt);
            const std::uint64_t blocksWide = (static_cast<std::uint64_t>(width)  + bx - 1u) / bx;
            const std::uint64_t blocksHigh = (static_cast<std::uint64_t>(height) + bx - 1u) / bx;
            return blocksWide * blocksHigh * depth * blockBytes;
        }

        return static_cast<std::uint64_t>(width) * height * depth * blockBytes;
    }

    // ----------------------------------------------------------
    // Packed full-chain layout
    // ----------------------------------------------------------

    /// One entry in `TextureUploadLayout::Subresources`. Describes
    /// where the bytes for `(MipLevel, ArrayLayer)` live in the
    /// packed staging buffer plus the per-mip texel extents the
    /// backend must hand to its native copy primitive (e.g.
    /// `VkBufferImageCopy::imageExtent` for Vulkan).
    struct TextureUploadSubresource
    {
        std::uint32_t MipLevel    = 0;
        std::uint32_t ArrayLayer  = 0;
        std::uint32_t Width       = 0;  // texels (mip extent)
        std::uint32_t Height      = 0;  // texels (mip extent)
        std::uint32_t Depth       = 1;  // texels; >1 only for Tex3D
        std::uint64_t OffsetBytes = 0;
        std::uint64_t SizeBytes   = 0;
    };

    /// Packed CPU-side description of a multi-subresource upload.
    ///
    /// `Subresources` is layer-major / mip-minor: for `ArrayLayers L`
    /// and `MipLevels M`, the order is
    /// `(layer=0,mip=0), (layer=0,mip=1), ..., (layer=0,mip=M-1),`
    /// `(layer=1,mip=0), ..., (layer=L-1,mip=M-1)`. This matches the
    /// DDS / KTX convention most asset pipelines emit and lets the
    /// backend coalesce all per-(mip,layer) regions into a single
    /// `VkBufferImageCopy[]` array against one staging buffer.
    ///
    /// `TotalBytes` is the size of the flat staging buffer the caller
    /// must allocate and fill with all per-subresource bytes
    /// concatenated in the same order, with each subresource starting
    /// at `Subresources[i].OffsetBytes` (no inter-subresource padding;
    /// per-subresource bytes are tightly packed by their block
    /// layout).
    struct TextureUploadLayout
    {
        std::uint64_t TotalBytes = 0;
        std::vector<TextureUploadSubresource> Subresources;
    };

    /// Compute the full-mip-chain upload layout for `desc`.
    ///
    /// Returns:
    ///   - `InvalidArgument` when extents or counts are zero, when
    ///     `MipLevels == 0`, or when `DepthOrArrayLayers == 0` for a
    ///     dimensionality that interprets it as layers/depth.
    ///   - `InvalidFormat` for `Format::Undefined`, depth-stencil
    ///     formats, or any other format whose `BytesPerBlock` is 0.
    ///   - On success, a layout whose `Subresources.size()` equals
    ///     `MipLevels * ArrayLayers` for 2D / Cube and `MipLevels`
    ///     for `Tex3D` (a single layer with per-mip 3D extents).
    ///
    /// Tex1D and Tex3D are accepted by the math but the current
    /// upload path is intended for 2D / Cube sampled textures; 3D /
    /// 1D backend support remains the responsibility of slice A.2 /
    /// slice B if and when those callers exist.
    [[nodiscard]] inline Core::Expected<TextureUploadLayout> ComputeFullChainUploadLayout(
        const TextureDesc& desc) noexcept
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

                TextureUploadSubresource sub{};
                sub.MipLevel    = mip;
                sub.ArrayLayer  = layer;
                sub.Width       = MipExtent(desc.Width,  mip);
                sub.Height      = MipExtent(desc.Height, mip);
                sub.Depth       = is3D ? MipExtent(desc.DepthOrArrayLayers, mip) : 1u;
                sub.OffsetBytes = cursor;
                sub.SizeBytes   = bytes;
                layout.Subresources.push_back(sub);

                cursor += bytes;
            }
        }
        layout.TotalBytes = cursor;
        return Core::Ok(std::move(layout));
    }
}
