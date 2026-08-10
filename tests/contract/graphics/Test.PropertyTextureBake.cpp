#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

import Extrinsic.Core.Error;
import Extrinsic.Graphics.PropertyTextureBake;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

using namespace Extrinsic;

TEST(PropertyTextureBake, PipelineContractIsUvRasterWithoutDepthOrCulling)
{
    const RHI::PipelineDesc desc =
        Graphics::MakePropertyTextureBakePipelineDesc(
            "property.vert.spv",
            "property.frag.spv",
            RHI::Format::R32_FLOAT);

    EXPECT_EQ(desc.VertexShaderPath, "property.vert.spv");
    EXPECT_EQ(desc.FragmentShaderPath, "property.frag.spv");
    EXPECT_EQ(desc.Rasterizer.Culling, RHI::CullMode::None);
    EXPECT_FALSE(desc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(desc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(desc.ColorTargetCount, 1u);
    EXPECT_EQ(desc.ColorTargetFormats[0], RHI::Format::R32_FLOAT);
    EXPECT_EQ(
        desc.PushConstantSize,
        sizeof(Graphics::PropertyTextureBakePushConstants));
}

TEST(PropertyTextureBake, DilationPipelineAndScratchMatchEncodedOutput)
{
    const RHI::PipelineDesc pipeline =
        Graphics::MakePropertyTextureBakeDilationPipelineDesc(
            "fullscreen.vert.spv",
            "property-dilate.frag.spv",
            RHI::Format::RGBA8_UNORM);
    EXPECT_EQ(pipeline.VertexShaderPath, "fullscreen.vert.spv");
    EXPECT_EQ(
        pipeline.FragmentShaderPath,
        "property-dilate.frag.spv");
    EXPECT_EQ(pipeline.Rasterizer.Culling, RHI::CullMode::None);
    EXPECT_FALSE(pipeline.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(pipeline.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(
        pipeline.PushConstantSize,
        sizeof(Graphics::PropertyTextureBakeDilationPushConstants));

    const RHI::TextureDesc scratch =
        Graphics::MakePropertyTextureBakeDilationScratchTextureDesc(
            32u,
            16u,
            RHI::Format::RGBA8_UNORM);
    EXPECT_EQ(scratch.Width, 32u);
    EXPECT_EQ(scratch.Height, 16u);
    EXPECT_EQ(scratch.Fmt, RHI::Format::RGBA8_UNORM);
    EXPECT_NE(
        static_cast<std::uint32_t>(scratch.Usage) &
            static_cast<std::uint32_t>(RHI::TextureUsage::Sampled),
        0u);
    EXPECT_NE(
        static_cast<std::uint32_t>(scratch.Usage) &
            static_cast<std::uint32_t>(RHI::TextureUsage::ColorTarget),
        0u);
}

TEST(PropertyTextureBake, InvalidResourcesFailBeforeRecordingCommands)
{
    Tests::MockCommandContext commands{};
    Graphics::PropertyTextureBakeRecordDesc desc{
        .Pipeline = RHI::PipelineHandle{1u, 1u},
        .OutputTexture = RHI::TextureHandle{2u, 1u},
        .IndexBuffer = RHI::BufferHandle{3u, 1u},
        .TexcoordBDA = 0x1000u,
        .PropertyBDA = 0x2000u,
        .IndexBDA = 0x3000u,
        .IndexCount = 3u,
        .Width = 16u,
        .Height = 8u,
        .Encoding = Graphics::PropertyTextureBakeEncoding::ScalarColormap,
        .RangeMin = 0.0f,
        .RangeMax = 1.0f,
    };

    const Core::Result missingColormap =
        Graphics::RecordPropertyTextureBake(commands, desc);
    ASSERT_FALSE(missingColormap.has_value());
    EXPECT_EQ(missingColormap.error(), Core::ErrorCode::InvalidArgument);
    EXPECT_TRUE(commands.Events.empty());

    desc.Encoding = Graphics::PropertyTextureBakeEncoding::Raw;
    desc.IndexCount = 4u;
    const Core::Result nonTriangleIndexCount =
        Graphics::RecordPropertyTextureBake(commands, desc);
    ASSERT_FALSE(nonTriangleIndexCount.has_value());
    EXPECT_EQ(
        nonTriangleIndexCount.error(),
        Core::ErrorCode::InvalidArgument);
    EXPECT_TRUE(commands.Events.empty());

    desc.IndexCount = 3u;
    desc.PaddingTexels = 1u;
    const Core::Result rawPadding =
        Graphics::RecordPropertyTextureBake(commands, desc);
    ASSERT_FALSE(rawPadding.has_value());
    EXPECT_EQ(rawPadding.error(), Core::ErrorCode::InvalidArgument);
    EXPECT_TRUE(commands.Events.empty());
}

TEST(PropertyTextureBake, RecordsIndexedRasterWithExactRepresentationMetadata)
{
    Tests::MockCommandContext commands{};
    const Graphics::PropertyTextureBakeRecordDesc desc{
        .Pipeline = RHI::PipelineHandle{4u, 1u},
        .OutputTexture = RHI::TextureHandle{5u, 1u},
        .IndexBuffer = RHI::BufferHandle{6u, 1u},
        .TexcoordBDA = 0x1100u,
        .PropertyBDA = 0x2200u,
        .IndexBDA = 0x3300u,
        .FirstIndex = 9u,
        .IndexCount = 6u,
        .Width = 32u,
        .Height = 16u,
        .Domain = Graphics::PropertyTextureBakeDomain::NearestEdge,
        .ValueKind = Graphics::PropertyTextureBakeValueKind::Scalar,
        .Encoding = Graphics::PropertyTextureBakeEncoding::ScalarColormap,
        .ColormapID = 17u,
        .RangeMin = -2.0f,
        .RangeMax = 6.0f,
        .InitialLayout = RHI::TextureLayout::Undefined,
        .FinalLayout = RHI::TextureLayout::TransferSrc,
    };

    ASSERT_TRUE(
        Graphics::RecordPropertyTextureBake(commands, desc).has_value());
    ASSERT_EQ(commands.TextureBarrierCalls.size(), 2u);
    EXPECT_EQ(commands.TextureBarrierCalls[0].Texture, desc.OutputTexture);
    EXPECT_EQ(
        commands.TextureBarrierCalls[0].After,
        RHI::TextureLayout::ColorAttachment);
    EXPECT_EQ(
        commands.TextureBarrierCalls[1].After,
        RHI::TextureLayout::TransferSrc);
    EXPECT_EQ(commands.BindPipelineCalls, 1);
    EXPECT_EQ(commands.LastBoundPipeline, desc.Pipeline);
    EXPECT_EQ(commands.BindIndexBufferCalls, 1);
    EXPECT_EQ(commands.LastIndexBuffer, desc.IndexBuffer);
    EXPECT_EQ(commands.LastIndexType, RHI::IndexType::Uint32);
    EXPECT_EQ(commands.DrawIndexedCalls, 1);
    EXPECT_EQ(commands.LastDrawIndexed.IndexCount, desc.IndexCount);
    EXPECT_EQ(commands.LastDrawIndexed.FirstIndex, desc.FirstIndex);

    ASSERT_EQ(commands.PushConstantPayloads.size(), 1u);
    ASSERT_EQ(
        commands.PushConstantPayloads[0].size(),
        sizeof(Graphics::PropertyTextureBakePushConstants));
    Graphics::PropertyTextureBakePushConstants push{};
    std::memcpy(
        &push,
        commands.PushConstantPayloads[0].data(),
        sizeof(push));
    EXPECT_EQ(push.TexcoordBDA, desc.TexcoordBDA);
    EXPECT_EQ(push.PropertyBDA, desc.PropertyBDA);
    EXPECT_EQ(push.IndexBDA, desc.IndexBDA);
    EXPECT_EQ(
        push.Domain,
        static_cast<std::uint32_t>(desc.Domain));
    EXPECT_EQ(
        push.Encoding,
        static_cast<std::uint32_t>(desc.Encoding));
    EXPECT_EQ(push.ColormapID, desc.ColormapID);
    EXPECT_FLOAT_EQ(push.RangeMin, desc.RangeMin);
    EXPECT_FLOAT_EQ(push.RangeMax, desc.RangeMax);
}

TEST(PropertyTextureBake, RecordsEncodedPaddingThroughGenericDilation)
{
    Tests::MockCommandContext commands{};
    const Graphics::PropertyTextureBakeRecordDesc desc{
        .Pipeline = RHI::PipelineHandle{4u, 1u},
        .OutputTexture = RHI::TextureHandle{5u, 1u},
        .Dilation = Graphics::PropertyTextureBakeDilationResources{
            .Pipeline = RHI::PipelineHandle{7u, 1u},
            .ScratchTexture = RHI::TextureHandle{8u, 1u},
        },
        .IndexBuffer = RHI::BufferHandle{6u, 1u},
        .TexcoordBDA = 0x1100u,
        .PropertyBDA = 0x2200u,
        .IndexBDA = 0x3300u,
        .IndexCount = 3u,
        .Width = 32u,
        .Height = 16u,
        .PaddingTexels = 2u,
        .ValueKind = Graphics::PropertyTextureBakeValueKind::Vector3,
        .Encoding = Graphics::PropertyTextureBakeEncoding::Normal,
        .RangeMin = 0.0f,
        .RangeMax = 1.0f,
        .FinalLayout = RHI::TextureLayout::TransferSrc,
    };

    ASSERT_TRUE(
        Graphics::RecordPropertyTextureBake(commands, desc).has_value());
    ASSERT_EQ(commands.SampledTextureBindings.size(), 2u);
    EXPECT_EQ(
        commands.SampledTextureBindings[0].Texture,
        desc.OutputTexture);
    EXPECT_EQ(
        commands.SampledTextureBindings[0].DescriptorIndex,
        Graphics::kPropertyTextureBakeDilationOutputDescriptorSlot);
    EXPECT_EQ(
        commands.SampledTextureBindings[1].Texture,
        desc.Dilation.ScratchTexture);
    EXPECT_EQ(
        commands.SampledTextureBindings[1].DescriptorIndex,
        Graphics::kPropertyTextureBakeDilationScratchDescriptorSlot);
    EXPECT_EQ(commands.DrawIndexedCalls, 1);
    EXPECT_EQ(commands.DrawCalls, 2);
    ASSERT_EQ(commands.PushConstantPayloads.size(), 3u);
    EXPECT_EQ(
        commands.PushConstantPayloads[0].size(),
        sizeof(Graphics::PropertyTextureBakePushConstants));
    EXPECT_EQ(
        commands.PushConstantPayloads[1].size(),
        sizeof(Graphics::PropertyTextureBakeDilationPushConstants));
    EXPECT_EQ(
        commands.PushConstantPayloads[2].size(),
        sizeof(Graphics::PropertyTextureBakeDilationPushConstants));
    for (std::size_t index = 1u; index < 3u; ++index)
    {
        Graphics::PropertyTextureBakeDilationPushConstants push{};
        std::memcpy(
            &push,
            commands.PushConstantPayloads[index].data(),
            sizeof(push));
        EXPECT_FLOAT_EQ(push.ClearR, 0.0f);
        EXPECT_FLOAT_EQ(push.ClearG, 0.0f);
        EXPECT_FLOAT_EQ(push.ClearB, 0.0f);
        EXPECT_FLOAT_EQ(push.ClearA, 0.0f);
    }
    ASSERT_FALSE(commands.TextureBarrierCalls.empty());
    EXPECT_EQ(
        commands.TextureBarrierCalls.back().Texture,
        desc.OutputTexture);
    EXPECT_EQ(
        commands.TextureBarrierCalls.back().After,
        RHI::TextureLayout::TransferSrc);
}
