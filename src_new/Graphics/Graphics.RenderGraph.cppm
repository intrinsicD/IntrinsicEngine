module;

#include <cstdint>
#include <memory>
#include <string>

export module Extrinsic.Graphics.RenderGraph;

import Extrinsic.Core.Error;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;

export import :Resources;
export import :Pass;
export import :Compiler;
export import :Barriers;
export import :TransientAllocator;
export import :Executor;

namespace Extrinsic::Graphics
{
    export class RenderGraph final
    {
    public:
        RenderGraph();
        ~RenderGraph();

        RenderGraph(RenderGraph&&) noexcept;
        RenderGraph& operator=(RenderGraph&&) noexcept;

        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        [[nodiscard]] PassRef AddPass(std::string name, bool sideEffect = false);
        [[nodiscard]] TextureRef ImportBackbuffer(std::string name, RHI::TextureHandle handle);
        [[nodiscard]] TextureRef ImportTexture(std::string name,
                                               RHI::TextureHandle handle,
                                               TextureState initial,
                                               TextureState finalState = TextureState::Present);
        [[nodiscard]] BufferRef ImportBuffer(std::string name,
                                             RHI::BufferHandle handle,
                                             BufferState initial,
                                             BufferState finalState = BufferState::Undefined);
        [[nodiscard]] TextureRef CreateTexture(std::string name, const RHI::TextureDesc& desc);
        [[nodiscard]] BufferRef CreateBuffer(std::string name, const RHI::BufferDesc& desc);

        [[nodiscard]] Core::Expected<CompiledRenderGraph> Compile() const;
        [[nodiscard]] Core::Result ValidateTextureRef(TextureRef ref) const;
        [[nodiscard]] Core::Result ValidateBufferRef(BufferRef ref) const;
        [[nodiscard]] const TextureResourceDesc* GetTextureDesc(TextureRef ref) const;
        [[nodiscard]] const BufferResourceDesc* GetBufferDesc(BufferRef ref) const;
        void Reset();

        [[nodiscard]] std::uint32_t GetPassCount() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
