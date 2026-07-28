module;

#include <array>
#include <cstdint>
#include <string>
#include <utility>

module Extrinsic.Graphics.PropertyTextureBake;

import Extrinsic.RHI.Types;

namespace Extrinsic::Graphics
{
    namespace
    {
        struct BakeClearColor
        {
            float R{0.0f};
            float G{0.0f};
            float B{0.0f};
            float A{0.0f};
        };

        [[nodiscard]] BakeClearColor ClearColorFor(
            const PropertyTextureBakeEncoding encoding) noexcept
        {
            return encoding == PropertyTextureBakeEncoding::Normal
                ? BakeClearColor{
                      .R = 0.5f,
                      .G = 0.5f,
                      .B = 1.0f,
                  }
                : BakeClearColor{};
        }

        struct TextureRecordingState
        {
            RHI::TextureHandle Handle{};
            RHI::TextureLayout Layout{RHI::TextureLayout::Undefined};
        };

        void TransitionTexture(
            RHI::ICommandContext& commandContext,
            TextureRecordingState& state,
            const RHI::TextureLayout next)
        {
            if (state.Layout == next)
                return;
            commandContext.TextureBarrier(state.Handle, state.Layout, next);
            state.Layout = next;
        }

        void BeginColorPass(
            RHI::ICommandContext& commandContext,
            const RHI::TextureHandle target,
            const std::uint32_t width,
            const std::uint32_t height,
            const RHI::LoadOp load,
            const RHI::PipelineHandle pipeline,
            const BakeClearColor clear)
        {
            const std::array<RHI::ColorAttachment, 1u> attachments{{
                RHI::ColorAttachment{
                    .Target = target,
                    .Load = load,
                    .Store = RHI::StoreOp::Store,
                    .ClearR = clear.R,
                    .ClearG = clear.G,
                    .ClearB = clear.B,
                    .ClearA = clear.A,
                },
            }};
            commandContext.BeginRenderPass(RHI::RenderPassDesc{
                .ColorTargets = attachments,
            });
            commandContext.SetViewport(
                0.0f,
                0.0f,
                static_cast<float>(width),
                static_cast<float>(height),
                0.0f,
                1.0f);
            commandContext.SetScissor(0, 0, width, height);
            commandContext.BindPipeline(pipeline);
        }

        void RecordRasterPass(
            RHI::ICommandContext& commandContext,
            const PropertyTextureBakeRecordDesc& desc,
            const RHI::TextureHandle target,
            const BakeClearColor clear)
        {
            BeginColorPass(
                commandContext,
                target,
                desc.Width,
                desc.Height,
                RHI::LoadOp::Clear,
                desc.Pipeline,
                clear);
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
        }

        void RecordDilationPass(
            RHI::ICommandContext& commandContext,
            const PropertyTextureBakeRecordDesc& desc,
            const RHI::TextureHandle target,
            const std::uint32_t sourceDescriptorSlot,
            const BakeClearColor clear)
        {
            BeginColorPass(
                commandContext,
                target,
                desc.Width,
                desc.Height,
                RHI::LoadOp::DontCare,
                desc.Dilation.Pipeline,
                clear);
            const PropertyTextureBakeDilationPushConstants push{
                .SourceTextureSlot = sourceDescriptorSlot,
                .ClearR = clear.R,
                .ClearG = clear.G,
                .ClearB = clear.B,
                .ClearA = clear.A,
            };
            commandContext.PushConstants(
                &push,
                static_cast<std::uint32_t>(sizeof(push)),
                0u);
            commandContext.Draw(3u, 1u, 0u, 0u);
            commandContext.EndRenderPass();
        }
    }

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

    RHI::PipelineDesc MakePropertyTextureBakeDilationPipelineDesc(
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
            sizeof(PropertyTextureBakeDilationPushConstants));
        desc.DebugName = "PropertyTextureBake.Dilation";
        return desc;
    }

    RHI::TextureDesc MakePropertyTextureBakeDilationScratchTextureDesc(
        const std::uint32_t width,
        const std::uint32_t height,
        const RHI::Format colorFormat,
        const char* const debugName) noexcept
    {
        return RHI::TextureDesc{
            .Width = width,
            .Height = height,
            .MipLevels = 1u,
            .Fmt = colorFormat,
            .Usage = RHI::TextureUsage::Sampled |
                     RHI::TextureUsage::ColorTarget,
            .InitialLayout = RHI::TextureLayout::Undefined,
            .DebugName = debugName != nullptr
                ? debugName
                : "PropertyTextureBake.DilationScratch",
        };
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
            desc.PaddingTexels > kPropertyTextureBakeMaxPaddingTexels ||
            (desc.Encoding == PropertyTextureBakeEncoding::ScalarColormap &&
             desc.ColormapID == 0u) ||
            (desc.PaddingTexels != 0u &&
             (desc.Encoding == PropertyTextureBakeEncoding::Raw ||
              !desc.Dilation.IsValid() ||
              desc.Dilation.ScratchTexture == desc.OutputTexture)) ||
            !(desc.RangeMin < desc.RangeMax))
        {
            return Core::Err(Core::ErrorCode::InvalidArgument);
        }

        const BakeClearColor clear = ClearColorFor(desc.Encoding);
        TextureRecordingState output{
            .Handle = desc.OutputTexture,
            .Layout = desc.InitialLayout,
        };
        if (desc.PaddingTexels == 0u)
        {
            TransitionTexture(
                commandContext,
                output,
                RHI::TextureLayout::ColorAttachment);
            RecordRasterPass(
                commandContext,
                desc,
                desc.OutputTexture,
                clear);
            TransitionTexture(
                commandContext,
                output,
                desc.FinalLayout);
            return Core::Ok();
        }

        TextureRecordingState scratch{
            .Handle = desc.Dilation.ScratchTexture,
            .Layout = desc.Dilation.ScratchInitialLayout,
        };
        commandContext.BindFrameSampledTextureAt(
            desc.OutputTexture,
            desc.Dilation.OutputDescriptorSlot);
        commandContext.BindFrameSampledTextureAt(
            desc.Dilation.ScratchTexture,
            desc.Dilation.ScratchDescriptorSlot);

        const bool rasterToOutput = (desc.PaddingTexels % 2u) == 0u;
        TextureRecordingState& rasterTarget =
            rasterToOutput ? output : scratch;
        TransitionTexture(
            commandContext,
            rasterTarget,
            RHI::TextureLayout::ColorAttachment);
        RecordRasterPass(
            commandContext,
            desc,
            rasterTarget.Handle,
            clear);
        TransitionTexture(
            commandContext,
            rasterTarget,
            RHI::TextureLayout::ShaderReadOnly);

        bool sourceIsOutput = rasterToOutput;
        for (std::uint32_t pass = 0u;
             pass < desc.PaddingTexels;
             ++pass)
        {
            const bool targetIsOutput = !sourceIsOutput;
            TextureRecordingState& target =
                targetIsOutput ? output : scratch;
            const std::uint32_t sourceSlot = sourceIsOutput
                ? desc.Dilation.OutputDescriptorSlot
                : desc.Dilation.ScratchDescriptorSlot;
            const bool lastPass =
                (pass + 1u) == desc.PaddingTexels;

            TransitionTexture(
                commandContext,
                target,
                RHI::TextureLayout::ColorAttachment);
            RecordDilationPass(
                commandContext,
                desc,
                target.Handle,
                sourceSlot,
                clear);
            TransitionTexture(
                commandContext,
                target,
                lastPass && targetIsOutput
                    ? desc.FinalLayout
                    : RHI::TextureLayout::ShaderReadOnly);
            sourceIsOutput = targetIsOutput;
        }
        return Core::Ok();
    }
}
