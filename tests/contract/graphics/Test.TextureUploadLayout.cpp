#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

import Extrinsic.Backends.Null;
import Extrinsic.Core.Error;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.TextureUpload;

namespace
{
    namespace RHI = Extrinsic::RHI;
    namespace Core = Extrinsic::Core;

    using RHI::Format;
    using RHI::TextureDesc;
    using RHI::TextureDimension;
    using RHI::TextureUsage;

    [[nodiscard]] TextureDesc Make2D(std::uint32_t w,
                                     std::uint32_t h,
                                     std::uint32_t mips,
                                     std::uint32_t layers = 1u,
                                     Format fmt = Format::RGBA8_UNORM)
    {
        TextureDesc desc{};
        desc.Width              = w;
        desc.Height             = h;
        desc.DepthOrArrayLayers = layers;
        desc.MipLevels          = mips;
        desc.Fmt                = fmt;
        desc.Dimension          = TextureDimension::Tex2D;
        desc.Usage              = TextureUsage::Sampled | TextureUsage::TransferDst;
        return desc;
    }
}

// ---------- Format-level helpers --------------------------------------------

TEST(TextureUploadFormat, BytesPerBlockMatchesUploadContract)
{
    EXPECT_EQ(RHI::BytesPerBlock(Format::R8_UNORM),     1u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::RG8_UNORM),    2u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::RGBA8_UNORM),  4u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::RGBA8_SRGB),   4u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::BGRA8_UNORM),  4u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::R16_FLOAT),    2u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::RG16_FLOAT),   4u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::RGBA16_FLOAT), 8u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::R32_FLOAT),    4u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::RG32_FLOAT),   8u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::RGB32_FLOAT),  12u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::RGBA32_FLOAT), 16u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::BC1_UNORM),    8u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::BC3_UNORM),    16u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::BC5_UNORM),    16u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::BC7_UNORM),    16u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::BC7_SRGB),     16u);
}

TEST(TextureUploadFormat, DepthStencilAndUndefinedAreNotUploadable)
{
    EXPECT_EQ(RHI::BytesPerBlock(Format::Undefined),         0u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::D16_UNORM),         0u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::D32_FLOAT),         0u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::D24_UNORM_S8_UINT), 0u);
    EXPECT_EQ(RHI::BytesPerBlock(Format::D32_FLOAT_S8_UINT), 0u);

    EXPECT_TRUE(RHI::IsDepthStencilFormat(Format::D16_UNORM));
    EXPECT_TRUE(RHI::IsDepthStencilFormat(Format::D32_FLOAT));
    EXPECT_TRUE(RHI::IsDepthStencilFormat(Format::D24_UNORM_S8_UINT));
    EXPECT_TRUE(RHI::IsDepthStencilFormat(Format::D32_FLOAT_S8_UINT));
    EXPECT_FALSE(RHI::IsDepthStencilFormat(Format::RGBA8_UNORM));

    EXPECT_FALSE(RHI::IsUploadableFormat(Format::Undefined));
    EXPECT_FALSE(RHI::IsUploadableFormat(Format::D32_FLOAT));
    EXPECT_TRUE(RHI::IsUploadableFormat(Format::RGBA8_UNORM));
    EXPECT_TRUE(RHI::IsUploadableFormat(Format::BC7_SRGB));
}

TEST(TextureUploadFormat, BlockExtentIsFourForBlockCompressed)
{
    EXPECT_EQ(RHI::BlockExtent(Format::RGBA8_UNORM), 1u);
    EXPECT_EQ(RHI::BlockExtent(Format::R32_FLOAT),   1u);
    EXPECT_EQ(RHI::BlockExtent(Format::BC1_UNORM),   4u);
    EXPECT_EQ(RHI::BlockExtent(Format::BC7_SRGB),    4u);

    EXPECT_TRUE(RHI::IsBlockCompressedFormat(Format::BC1_UNORM));
    EXPECT_TRUE(RHI::IsBlockCompressedFormat(Format::BC3_UNORM));
    EXPECT_TRUE(RHI::IsBlockCompressedFormat(Format::BC5_UNORM));
    EXPECT_TRUE(RHI::IsBlockCompressedFormat(Format::BC7_UNORM));
    EXPECT_TRUE(RHI::IsBlockCompressedFormat(Format::BC7_SRGB));
    EXPECT_FALSE(RHI::IsBlockCompressedFormat(Format::RGBA8_UNORM));
}

// ---------- MipExtent --------------------------------------------------------

TEST(TextureUploadMip, MipExtentClampsToOne)
{
    EXPECT_EQ(RHI::MipExtent(256u, 0u), 256u);
    EXPECT_EQ(RHI::MipExtent(256u, 1u), 128u);
    EXPECT_EQ(RHI::MipExtent(256u, 8u), 1u);
    EXPECT_EQ(RHI::MipExtent(256u, 9u), 1u);  // clamped, not zero
    EXPECT_EQ(RHI::MipExtent(1u,   0u), 1u);
    EXPECT_EQ(RHI::MipExtent(1u,   5u), 1u);
}

// ---------- ComputeSubresourceUploadSize ------------------------------------

TEST(TextureUploadSize, RGBA8MatchesNaiveProduct)
{
    const TextureDesc desc = Make2D(4u, 4u, 3u);  // 4x4, 2x2, 1x1
    EXPECT_EQ(RHI::ComputeSubresourceUploadSize(desc, 0u), 4u * 4u * 4u);
    EXPECT_EQ(RHI::ComputeSubresourceUploadSize(desc, 1u), 2u * 2u * 4u);
    EXPECT_EQ(RHI::ComputeSubresourceUploadSize(desc, 2u), 1u * 1u * 4u);
}

TEST(TextureUploadSize, NonSquareAndNonPowerOfTwoMipsClampToOne)
{
    // 6x3 RGBA8: mips advance to (3,1), (1,1), (1,1)...
    const TextureDesc desc = Make2D(6u, 3u, 4u);
    EXPECT_EQ(RHI::ComputeSubresourceUploadSize(desc, 0u), 6u * 3u * 4u);
    EXPECT_EQ(RHI::ComputeSubresourceUploadSize(desc, 1u), 3u * 1u * 4u);
    EXPECT_EQ(RHI::ComputeSubresourceUploadSize(desc, 2u), 1u * 1u * 4u);
    EXPECT_EQ(RHI::ComputeSubresourceUploadSize(desc, 3u), 1u * 1u * 4u);
}

TEST(TextureUploadSize, BlockCompressedRoundsUpToBlockExtent)
{
    // BC7 (16 B/block, 4x4 block extent).
    // 6x6 mip 0 → ceil(6/4) * ceil(6/4) = 2 * 2 = 4 blocks = 64 B.
    // 3x3 mip 1 → ceil(3/4) * ceil(3/4) = 1 * 1 = 1 block  = 16 B.
    // 1x1 mip 2 → ceil(1/4) * ceil(1/4) = 1 * 1 = 1 block  = 16 B.
    const TextureDesc desc = Make2D(6u, 6u, 3u, 1u, Format::BC7_UNORM);
    EXPECT_EQ(RHI::ComputeSubresourceUploadSize(desc, 0u), 64u);
    EXPECT_EQ(RHI::ComputeSubresourceUploadSize(desc, 1u), 16u);
    EXPECT_EQ(RHI::ComputeSubresourceUploadSize(desc, 2u), 16u);
}

TEST(TextureUploadSize, OutOfRangeMipReturnsZero)
{
    const TextureDesc desc = Make2D(4u, 4u, 3u);
    EXPECT_EQ(RHI::ComputeSubresourceUploadSize(desc, 3u), 0u);
    EXPECT_EQ(RHI::ComputeSubresourceUploadSize(desc, 99u), 0u);
}

TEST(TextureUploadSize, UnsupportedFormatReturnsZero)
{
    const TextureDesc undef = Make2D(4u, 4u, 1u, 1u, Format::Undefined);
    EXPECT_EQ(RHI::ComputeSubresourceUploadSize(undef, 0u), 0u);

    const TextureDesc depth = Make2D(4u, 4u, 1u, 1u, Format::D32_FLOAT);
    EXPECT_EQ(RHI::ComputeSubresourceUploadSize(depth, 0u), 0u);
}

// ---------- ComputeFullChainUploadLayout ------------------------------------

TEST(TextureUploadLayout, SingleLayerMipChainPacksLayerMajor)
{
    // 4x4 RGBA8 with full mip chain (3 mips): expect 4*4*4 + 2*2*4 + 1*1*4 = 84 bytes.
    const TextureDesc desc = Make2D(4u, 4u, 3u, 1u);
    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    ASSERT_TRUE(layoutOr.has_value());
    const auto& layout = *layoutOr;

    ASSERT_EQ(layout.Subresources.size(), 3u);
    EXPECT_EQ(layout.TotalBytes, 84u);

    EXPECT_EQ(layout.Subresources[0].MipLevel,    0u);
    EXPECT_EQ(layout.Subresources[0].ArrayLayer,  0u);
    EXPECT_EQ(layout.Subresources[0].Width,       4u);
    EXPECT_EQ(layout.Subresources[0].Height,      4u);
    EXPECT_EQ(layout.Subresources[0].Depth,       1u);
    EXPECT_EQ(layout.Subresources[0].OffsetBytes, 0u);
    EXPECT_EQ(layout.Subresources[0].SizeBytes,   64u);

    EXPECT_EQ(layout.Subresources[1].MipLevel,    1u);
    EXPECT_EQ(layout.Subresources[1].OffsetBytes, 64u);
    EXPECT_EQ(layout.Subresources[1].SizeBytes,   16u);
    EXPECT_EQ(layout.Subresources[1].Width,       2u);
    EXPECT_EQ(layout.Subresources[1].Height,      2u);

    EXPECT_EQ(layout.Subresources[2].MipLevel,    2u);
    EXPECT_EQ(layout.Subresources[2].OffsetBytes, 80u);
    EXPECT_EQ(layout.Subresources[2].SizeBytes,   4u);
    EXPECT_EQ(layout.Subresources[2].Width,       1u);
    EXPECT_EQ(layout.Subresources[2].Height,      1u);
}

TEST(TextureUploadLayout, MultiLayerOrderingIsLayerMajorMipMinor)
{
    // 2 layers x 2 mips of 2x2 RGBA8: expect 4 entries in (layer, mip) order
    // (0,0), (0,1), (1,0), (1,1).
    const TextureDesc desc = Make2D(2u, 2u, 2u, 2u);
    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    ASSERT_TRUE(layoutOr.has_value());
    const auto& layout = *layoutOr;

    ASSERT_EQ(layout.Subresources.size(), 4u);
    // Mip 0 size = 16 B, mip 1 size = 4 B → per-layer block = 20 B.
    EXPECT_EQ(layout.TotalBytes, 40u);

    EXPECT_EQ(layout.Subresources[0].ArrayLayer, 0u);
    EXPECT_EQ(layout.Subresources[0].MipLevel,   0u);
    EXPECT_EQ(layout.Subresources[0].OffsetBytes, 0u);

    EXPECT_EQ(layout.Subresources[1].ArrayLayer, 0u);
    EXPECT_EQ(layout.Subresources[1].MipLevel,   1u);
    EXPECT_EQ(layout.Subresources[1].OffsetBytes, 16u);

    EXPECT_EQ(layout.Subresources[2].ArrayLayer, 1u);
    EXPECT_EQ(layout.Subresources[2].MipLevel,   0u);
    EXPECT_EQ(layout.Subresources[2].OffsetBytes, 20u);

    EXPECT_EQ(layout.Subresources[3].ArrayLayer, 1u);
    EXPECT_EQ(layout.Subresources[3].MipLevel,   1u);
    EXPECT_EQ(layout.Subresources[3].OffsetBytes, 36u);
}

TEST(TextureUploadLayout, CubeMapTreatsSixLayersAsFaces)
{
    TextureDesc desc = Make2D(8u, 8u, 1u, 6u);
    desc.Dimension = TextureDimension::TexCube;
    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    ASSERT_TRUE(layoutOr.has_value());
    const auto& layout = *layoutOr;

    ASSERT_EQ(layout.Subresources.size(), 6u);
    EXPECT_EQ(layout.TotalBytes, 6u * 8u * 8u * 4u);
    for (std::uint32_t face = 0u; face < 6u; ++face)
    {
        EXPECT_EQ(layout.Subresources[face].ArrayLayer, face);
        EXPECT_EQ(layout.Subresources[face].MipLevel,   0u);
        EXPECT_EQ(layout.Subresources[face].OffsetBytes, face * 8u * 8u * 4u);
    }
}

TEST(TextureUploadLayout, Tex3DTreatsDepthAsZAxisNotLayerCount)
{
    TextureDesc desc = Make2D(4u, 4u, 1u, 8u);
    desc.Dimension = TextureDimension::Tex3D;
    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    ASSERT_TRUE(layoutOr.has_value());
    const auto& layout = *layoutOr;

    // 3D collapses arrayLayers to 1; only mip count drives subresource count.
    ASSERT_EQ(layout.Subresources.size(), 1u);
    EXPECT_EQ(layout.Subresources[0].Width,  4u);
    EXPECT_EQ(layout.Subresources[0].Height, 4u);
    EXPECT_EQ(layout.Subresources[0].Depth,  8u);
    EXPECT_EQ(layout.TotalBytes, 4u * 4u * 8u * 4u);
}

TEST(TextureUploadLayout, BlockCompressedFullChainPacksRoundedSizes)
{
    // 8x8 BC7 with 4 mips: 8x8, 4x4, 2x2 (rounds to 4x4 block), 1x1 (rounds to 4x4 block).
    // Block sizes:  4 blocks (64 B), 1 block (16 B), 1 block (16 B), 1 block (16 B) = 112 B total.
    const TextureDesc desc = Make2D(8u, 8u, 4u, 1u, Format::BC7_UNORM);
    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    ASSERT_TRUE(layoutOr.has_value());
    const auto& layout = *layoutOr;

    ASSERT_EQ(layout.Subresources.size(), 4u);
    EXPECT_EQ(layout.Subresources[0].SizeBytes, 64u);
    EXPECT_EQ(layout.Subresources[1].SizeBytes, 16u);
    EXPECT_EQ(layout.Subresources[2].SizeBytes, 16u);
    EXPECT_EQ(layout.Subresources[3].SizeBytes, 16u);
    EXPECT_EQ(layout.TotalBytes, 112u);

    // Texel extents stay logical (2x2 for mip 2) even though byte size rounds up to 4x4 block.
    EXPECT_EQ(layout.Subresources[2].Width,  2u);
    EXPECT_EQ(layout.Subresources[2].Height, 2u);
    EXPECT_EQ(layout.Subresources[3].Width,  1u);
    EXPECT_EQ(layout.Subresources[3].Height, 1u);
}

TEST(TextureUploadLayout, RejectsDepthStencilFormat)
{
    const TextureDesc desc = Make2D(4u, 4u, 1u, 1u, Format::D32_FLOAT);
    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    ASSERT_FALSE(layoutOr.has_value());
    EXPECT_EQ(layoutOr.error(), Core::ErrorCode::InvalidFormat);
}

TEST(TextureUploadLayout, RejectsUndefinedFormat)
{
    const TextureDesc desc = Make2D(4u, 4u, 1u, 1u, Format::Undefined);
    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    ASSERT_FALSE(layoutOr.has_value());
    EXPECT_EQ(layoutOr.error(), Core::ErrorCode::InvalidFormat);
}

TEST(TextureUploadLayout, RejectsZeroExtentOrCount)
{
    auto zeroWidth = Make2D(0u, 4u, 1u);
    EXPECT_FALSE(RHI::ComputeFullChainUploadLayout(zeroWidth).has_value());

    auto zeroMips = Make2D(4u, 4u, 0u);
    EXPECT_FALSE(RHI::ComputeFullChainUploadLayout(zeroMips).has_value());

    auto zeroLayers = Make2D(4u, 4u, 1u, 0u);
    EXPECT_FALSE(RHI::ComputeFullChainUploadLayout(zeroLayers).has_value());
}

// ---------- Vulkan-safe bufferOffset alignment ------------------------------

TEST(TextureUploadAlignment, RequiredBufferOffsetAlignmentMatchesVulkanVUIDs)
{
    // max(4, BytesPerBlock(fmt)).
    EXPECT_EQ(RHI::RequiredBufferOffsetAlignment(Format::R8_UNORM),     4u);
    EXPECT_EQ(RHI::RequiredBufferOffsetAlignment(Format::RG8_UNORM),    4u);
    EXPECT_EQ(RHI::RequiredBufferOffsetAlignment(Format::RGBA8_UNORM),  4u);
    EXPECT_EQ(RHI::RequiredBufferOffsetAlignment(Format::R16_FLOAT),    4u);
    EXPECT_EQ(RHI::RequiredBufferOffsetAlignment(Format::RG16_FLOAT),   4u);
    EXPECT_EQ(RHI::RequiredBufferOffsetAlignment(Format::RGBA16_FLOAT), 8u);
    EXPECT_EQ(RHI::RequiredBufferOffsetAlignment(Format::R32_FLOAT),    4u);
    EXPECT_EQ(RHI::RequiredBufferOffsetAlignment(Format::RG32_FLOAT),   8u);
    EXPECT_EQ(RHI::RequiredBufferOffsetAlignment(Format::RGB32_FLOAT),  12u);
    EXPECT_EQ(RHI::RequiredBufferOffsetAlignment(Format::RGBA32_FLOAT), 16u);
    EXPECT_EQ(RHI::RequiredBufferOffsetAlignment(Format::BC1_UNORM),    8u);
    EXPECT_EQ(RHI::RequiredBufferOffsetAlignment(Format::BC7_UNORM),    16u);

    // Non-uploadable formats yield 0 alignment.
    EXPECT_EQ(RHI::RequiredBufferOffsetAlignment(Format::Undefined), 0u);
    EXPECT_EQ(RHI::RequiredBufferOffsetAlignment(Format::D32_FLOAT), 0u);
}

TEST(TextureUploadAlignment, R8MultiLayerOffsetsAreFourAligned)
{
    // 2 layers x 2 mips of 2x2 R8: per-mip byte counts 4 and 1.
    // Without alignment padding, layer 1 mip 0 would land at offset 5,
    // violating VUID-vkCmdCopyBufferToImage-commandBuffer-07737 on
    // transfer-only queues. With alignment max(4, 1) = 4 padding,
    // layer 1 mip 0 lands at offset 8 instead.
    const TextureDesc desc = Make2D(2u, 2u, 2u, 2u, Format::R8_UNORM);
    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    ASSERT_TRUE(layoutOr.has_value());
    const auto& layout = *layoutOr;

    ASSERT_EQ(layout.Subresources.size(), 4u);
    EXPECT_EQ(layout.Subresources[0].OffsetBytes, 0u);   // L0 M0: 4 B
    EXPECT_EQ(layout.Subresources[0].SizeBytes,   4u);
    EXPECT_EQ(layout.Subresources[1].OffsetBytes, 4u);   // L0 M1: 1 B (aligned to 4)
    EXPECT_EQ(layout.Subresources[1].SizeBytes,   1u);
    EXPECT_EQ(layout.Subresources[2].OffsetBytes, 8u);   // L1 M0: 4 B (aligned, was 5 unaligned)
    EXPECT_EQ(layout.Subresources[2].SizeBytes,   4u);
    EXPECT_EQ(layout.Subresources[3].OffsetBytes, 12u);  // L1 M1: 1 B (aligned, was 9 unaligned)
    EXPECT_EQ(layout.Subresources[3].SizeBytes,   1u);
    EXPECT_EQ(layout.TotalBytes, 13u);

    // Every offset must be a multiple of 4.
    for (const auto& sub : layout.Subresources)
        EXPECT_EQ(sub.OffsetBytes % 4u, 0u);
}

TEST(TextureUploadAlignment, RGB32FloatHasNonPowerOfTwoAlignment)
{
    // RGB32_FLOAT is 12 B/texel — alignment 12, not a power of two.
    // Two mips of 1x1 RGB32_FLOAT at 12 B each, two layers:
    //   (L=0,M=0) offset 0,  size 12, cursor 12
    //   (L=0,M=1) offset 12, size 12, cursor 24
    //   (L=1,M=0) offset 24, size 12, cursor 36
    //   (L=1,M=1) offset 36, size 12, cursor 48
    // All offsets divisible by 12 (and by 4) without explicit padding.
    const TextureDesc desc = Make2D(1u, 1u, 2u, 2u, Format::RGB32_FLOAT);
    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    ASSERT_TRUE(layoutOr.has_value());
    const auto& layout = *layoutOr;

    ASSERT_EQ(layout.Subresources.size(), 4u);
    for (std::size_t i = 0; i < layout.Subresources.size(); ++i)
    {
        EXPECT_EQ(layout.Subresources[i].OffsetBytes, i * 12u);
        EXPECT_EQ(layout.Subresources[i].SizeBytes,   12u);
        EXPECT_EQ(layout.Subresources[i].OffsetBytes % 12u, 0u);
        EXPECT_EQ(layout.Subresources[i].OffsetBytes % 4u,  0u);
    }
    EXPECT_EQ(layout.TotalBytes, 48u);
}

TEST(TextureUploadAlignment, RG8MultiLayerOddMipPadsToFour)
{
    // 2 layers x 2 mips of 3x3 RG8 (2 B/texel): mip 0 = 18 B, mip 1 = 2 B.
    //   (L=0,M=0) offset 0,  size 18, cursor 18
    //   (L=0,M=1) offset 20, size 2,  cursor 22  (cursor 18 -> 20 align to 4)
    //   (L=1,M=0) offset 24, size 18, cursor 42  (cursor 22 -> 24 align to 4)
    //   (L=1,M=1) offset 44, size 2,  cursor 46  (cursor 42 -> 44 align to 4)
    const TextureDesc desc = Make2D(3u, 3u, 2u, 2u, Format::RG8_UNORM);
    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    ASSERT_TRUE(layoutOr.has_value());
    const auto& layout = *layoutOr;

    ASSERT_EQ(layout.Subresources.size(), 4u);
    EXPECT_EQ(layout.Subresources[0].OffsetBytes, 0u);
    EXPECT_EQ(layout.Subresources[1].OffsetBytes, 20u);
    EXPECT_EQ(layout.Subresources[2].OffsetBytes, 24u);
    EXPECT_EQ(layout.Subresources[3].OffsetBytes, 44u);
    EXPECT_EQ(layout.TotalBytes, 46u);
    for (const auto& sub : layout.Subresources)
        EXPECT_EQ(sub.OffsetBytes % 4u, 0u);
}

TEST(TextureUploadAlignment, BC7OffsetsAreSixteenAligned)
{
    // BC7 element size 16 B. 6x6 + 3x3 + 1x1 textures all round to
    // 4x4-block size = 16 B per mip; offsets stack at 0, 16, 32 — all
    // multiples of 16 (and 4).
    const TextureDesc desc = Make2D(6u, 6u, 3u, 1u, Format::BC7_UNORM);
    auto layoutOr = RHI::ComputeFullChainUploadLayout(desc);
    ASSERT_TRUE(layoutOr.has_value());
    const auto& layout = *layoutOr;

    ASSERT_EQ(layout.Subresources.size(), 3u);
    for (const auto& sub : layout.Subresources)
    {
        EXPECT_EQ(sub.OffsetBytes % 16u, 0u);
        EXPECT_EQ(sub.OffsetBytes % 4u,  0u);
    }
}

// ---------- Transfer-queue full-chain seam -----------------------------------

TEST(TransferQueueFullChain, NullBackendRejectsFullChainUploadsFailClosed)
{
    std::unique_ptr<RHI::IDevice> device = Extrinsic::Backends::Null::CreateNullDevice();
    ASSERT_NE(device, nullptr);

    const RHI::TextureHandle texture = device->CreateTexture(Make2D(2u, 2u, 2u));
    ASSERT_TRUE(texture.IsValid());

    const std::array<std::byte, 20u> packedBytes{};
    RHI::ITransferQueue& queue = device->GetTransferQueue();

    const RHI::TransferToken token = queue.UploadTextureFullChain(texture, std::span<const std::byte>{packedBytes});
    EXPECT_FALSE(token.IsValid());
    EXPECT_TRUE(queue.IsComplete(token));
}

