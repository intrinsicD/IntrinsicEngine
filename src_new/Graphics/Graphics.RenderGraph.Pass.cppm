module;

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Graphics.RenderGraph:Pass;

import :Resources;
import Extrinsic.RHI.CommandContext;

namespace Extrinsic::Graphics
{
    export enum class RenderQueue : std::uint8_t
    {
        Graphics = 0,
        AsyncCompute,
        AsyncTransfer,
    };

    export enum class TextureUsage : std::uint8_t
    {
        ColorAttachmentRead = 0,
        ColorAttachmentWrite,
        DepthRead,
        DepthWrite,
        ShaderRead,
        ShaderWrite,
        TransferSrc,
        TransferDst,
        Present,
    };

    export enum class BufferUsage : std::uint8_t
    {
        IndirectRead = 0,
        IndexRead,
        VertexRead,
        ShaderRead,
        ShaderWrite,
        TransferSrc,
        TransferDst,
        HostReadback,
    };

    export struct PassRef
    {
        std::uint32_t Index = 0;
        std::uint32_t Generation = 0;

        [[nodiscard]] constexpr bool IsValid() const
        {
            return Generation != 0;
        }
    };

    struct TextureAccess
    {
        TextureRef Ref{};
        TextureUsage Usage = TextureUsage::ShaderRead;
        bool Write = false;
    };

    struct BufferAccess
    {
        BufferRef Ref{};
        BufferUsage Usage = BufferUsage::ShaderRead;
        bool Write = false;
    };

    export class RenderGraphBuilder;

    export struct RenderPassRecord
    {
        std::string Name{};
        bool SideEffect = false;
        RenderQueue Queue = RenderQueue::Graphics;
        std::vector<TextureAccess> TextureAccesses{};
        std::vector<BufferAccess> BufferAccesses{};
        bool HasRenderPassDesc = false;
        RHI::RenderPassDesc RenderPass{};
        bool HasValidationError = false;
    };

    export class RenderGraphBuilder final
    {
    public:
        TextureRef Read(TextureRef ref, TextureUsage usage);
        TextureRef Write(TextureRef ref, TextureUsage usage);
        BufferRef Read(BufferRef ref, BufferUsage usage);
        BufferRef Write(BufferRef ref, BufferUsage usage);

        void SetQueue(RenderQueue queue);
        void SetRenderPass(const RHI::RenderPassDesc& desc);
        void SideEffect();

    private:
        friend class RenderGraph;
        RenderGraphBuilder(RenderPassRecord& record,
                           std::move_only_function<bool(TextureRef, TextureUsage, bool)>&& textureValidator,
                           std::move_only_function<bool(BufferRef, BufferUsage, bool)>&& bufferValidator);

        RenderPassRecord* m_Record = nullptr;
        std::move_only_function<bool(TextureRef, TextureUsage, bool)> m_TextureValidator;
        std::move_only_function<bool(BufferRef, BufferUsage, bool)> m_BufferValidator;
    };
}
