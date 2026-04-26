#include <gtest/gtest.h>

#include <vector>
#include <array>

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;
import Extrinsic.RHI.Descriptors;

namespace
{
    class CountingCommandContext final : public Extrinsic::RHI::ICommandContext
    {
    public:
        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const Extrinsic::RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
        void BindPipeline(Extrinsic::RHI::PipelineHandle) override {}
        void BindIndexBuffer(Extrinsic::RHI::BufferHandle, std::uint64_t, Extrinsic::RHI::IndexType) override {}
        void PushConstants(const void*, std::uint32_t, std::uint32_t) override {}
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(Extrinsic::RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(Extrinsic::RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(Extrinsic::RHI::BufferHandle, std::uint64_t, Extrinsic::RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndirectCount(Extrinsic::RHI::BufferHandle, std::uint64_t, Extrinsic::RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(Extrinsic::RHI::BufferHandle, std::uint64_t) override {}

        void TextureBarrier(Extrinsic::RHI::TextureHandle texture,
                            Extrinsic::RHI::TextureLayout before,
                            Extrinsic::RHI::TextureLayout after) override
        {
            TextureCalls.push_back({texture, before, after});
        }

        void BufferBarrier(Extrinsic::RHI::BufferHandle buffer,
                           Extrinsic::RHI::MemoryAccess before,
                           Extrinsic::RHI::MemoryAccess after) override
        {
            BufferCalls.push_back({buffer, before, after});
        }

        void FillBuffer(Extrinsic::RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
        void CopyBuffer(Extrinsic::RHI::BufferHandle, Extrinsic::RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(Extrinsic::RHI::BufferHandle, std::uint64_t, Extrinsic::RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}

        struct TextureCall
        {
            Extrinsic::RHI::TextureHandle Texture{};
            Extrinsic::RHI::TextureLayout Before = Extrinsic::RHI::TextureLayout::Undefined;
            Extrinsic::RHI::TextureLayout After = Extrinsic::RHI::TextureLayout::Undefined;
        };

        struct BufferCall
        {
            Extrinsic::RHI::BufferHandle Buffer{};
            Extrinsic::RHI::MemoryAccess Before = Extrinsic::RHI::MemoryAccess::None;
            Extrinsic::RHI::MemoryAccess After = Extrinsic::RHI::MemoryAccess::None;
        };

        std::vector<TextureCall> TextureCalls{};
        std::vector<BufferCall> BufferCalls{};
    };
}

TEST(RHICommandContext, SubmitBarriersFallbackRoutesTextureAndBufferBarriers)
{
    CountingCommandContext context{};

    const std::array textures{
        Extrinsic::RHI::TextureBarrierDesc{
            .Texture = Extrinsic::RHI::TextureHandle{4u, 1u},
            .BeforeLayout = Extrinsic::RHI::TextureLayout::Undefined,
            .AfterLayout = Extrinsic::RHI::TextureLayout::ColorAttachment,
            .BeforeAccess = Extrinsic::RHI::MemoryAccess::None,
            .AfterAccess = Extrinsic::RHI::MemoryAccess::ShaderWrite,
        },
    };
    const std::array buffers{
        Extrinsic::RHI::BufferBarrierDesc{
            .Buffer = Extrinsic::RHI::BufferHandle{9u, 1u},
            .BeforeAccess = Extrinsic::RHI::MemoryAccess::ShaderWrite,
            .AfterAccess = Extrinsic::RHI::MemoryAccess::IndirectRead,
        },
    };
    const std::array memory{
        Extrinsic::RHI::MemoryBarrierDesc{
            .BeforeAccess = Extrinsic::RHI::MemoryAccess::TransferWrite,
            .AfterAccess = Extrinsic::RHI::MemoryAccess::ShaderRead,
        },
    };

    context.SubmitBarriers(Extrinsic::RHI::BarrierBatchDesc{
        .TextureBarriers = textures,
        .BufferBarriers = buffers,
        .MemoryBarriers = memory,
    });

    ASSERT_EQ(context.TextureCalls.size(), 1u);
    EXPECT_EQ(context.TextureCalls[0].Texture.Index, 4u);
    EXPECT_EQ(context.TextureCalls[0].Before, Extrinsic::RHI::TextureLayout::Undefined);
    EXPECT_EQ(context.TextureCalls[0].After, Extrinsic::RHI::TextureLayout::ColorAttachment);

    ASSERT_EQ(context.BufferCalls.size(), 1u);
    EXPECT_EQ(context.BufferCalls[0].Buffer.Index, 9u);
    EXPECT_EQ(context.BufferCalls[0].Before, Extrinsic::RHI::MemoryAccess::ShaderWrite);
    EXPECT_EQ(context.BufferCalls[0].After, Extrinsic::RHI::MemoryAccess::IndirectRead);
}
