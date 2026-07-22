module;

#include <array>
#include <cstdint>
#include <string>
#include <utility>

module Extrinsic.Graphics.PropertyTextureBake;

import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    RHI::PipelineDesc MakePropertyTextureBakePipelineDesc(
        std::string vertexShaderPath,
        std::string fragmentShaderPath,
        const RHI::Format colorFormat)
    {
        RHI::PipelineDesc desc{};
        desc.VertexShaderPath = std::move(vertexShaderPath);
        desc.FragmentShaderPath = std::move(fragmentShaderPath);
        desc.Rasterizer.Culling = RHI::CullMode::None;
        desc.DepthStencil.DepthTestEnable = false;
        desc.DepthStencil.DepthWriteEnable = false;
        desc.ColorTargetCount = 1u;
        desc.ColorTargetFormats[0] = colorFormat;
        desc.PushConstantSize = static_cast<std::uint32_t>(
            sizeof(PropertyTextureBakePushConstants));
        desc.DebugName = "PropertyTextureBake";
        return desc;
    }

    Core::Result RecordPropertyTextureBake(
        RHI::ICommandContext& commandContext,
        const PropertyTextureBakeRecordDesc& desc)
    {
        if (!desc.Pipeline.IsValid() ||
            !desc.OutputTexture.IsValid() ||
            !desc.IndexBuffer.IsValid() ||
            desc.TexcoordBDA == 0u ||
            desc.PropertyBDA == 0u ||
            desc.IndexBDA == 0u ||
            desc.IndexCount == 0u ||
            (desc.IndexCount % 3u) != 0u ||
            desc.Width == 0u ||
            desc.Height == 0u ||
            (desc.Encoding == PropertyTextureBakeEncoding::ScalarColormap &&
             desc.ColormapID == 0u) ||
            !(desc.RangeMin < desc.RangeMax))
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        commandContext.TextureBarrier(
            desc.OutputTexture,
            desc.InitialLayout,
            RHI::TextureLayout::ColorAttachment);

        const bool normal =
            desc.Encoding == PropertyTextureBakeEncoding::Normal;
        const std::array<RHI::ColorAttachment, 1u> attachments{{
            RHI::ColorAttachment{
                .Target = desc.OutputTexture,
                .Load = RHI::LoadOp::Clear,
                .Store = RHI::StoreOp::Store,
                .ClearR = normal ? 0.5f : 0.0f,
                .ClearG = normal ? 0.5f : 0.0f,
                .ClearB = normal ? 1.0f : 0.0f,
                .ClearA = 0.0f,
            },
        }};
        commandContext.BeginRenderPass(RHI::RenderPassDesc{
            .ColorTargets = attachments,
        });
        commandContext.SetViewport(
            0.0f,
            0.0f,
            static_cast<float>(desc.Width),
            static_cast<float>(desc.Height),
            0.0f,
            1.0f);
        commandContext.SetScissor(0, 0, desc.Width, desc.Height);
        commandContext.BindPipeline(desc.Pipeline);
        commandContext.BindIndexBuffer(
            desc.IndexBuffer,
            0u,
            RHI::IndexType::Uint32);

        const PropertyTextureBakePushConstants push{
            .TexcoordBDA = desc.TexcoordBDA,
            .PropertyBDA = desc.PropertyBDA,
            .IndexBDA = desc.IndexBDA,
            .Domain = static_cast<std::uint32_t>(desc.Domain),
            .ValueKind = static_cast<std::uint32_t>(desc.ValueKind),
            .Encoding = static_cast<std::uint32_t>(desc.Encoding),
            .ColormapID = desc.ColormapID,
            .RangeMin = desc.RangeMin,
            .RangeMax = desc.RangeMax,
        };
        commandContext.PushConstants(
            &push,
            static_cast<std::uint32_t>(sizeof(push)),
            0u);
        commandContext.DrawIndexed(
            desc.IndexCount,
            1u,
            desc.FirstIndex,
            0,
            0u);
        commandContext.EndRenderPass();
        commandContext.TextureBarrier(
            desc.OutputTexture,
            RHI::TextureLayout::ColorAttachment,
            desc.FinalLayout);
        return Core::Ok();
    }
}
