module;

#include <cstdint>
#include <vector>

export module Extrinsic.Graphics.RenderGraph:TransientAllocator;

import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;

namespace Extrinsic::Graphics
{
    export class TransientAllocator final
    {
    public:
        [[nodiscard]] RHI::TextureHandle AcquireTexture(const RHI::TextureDesc& desc);
        [[nodiscard]] RHI::BufferHandle AcquireBuffer(const RHI::BufferDesc& desc);
        void ReleaseTexture(RHI::TextureHandle handle);
        void ReleaseBuffer(RHI::BufferHandle handle);
        void ResetFrame();

    private:
        struct TextureEntry
        {
            RHI::TextureHandle Handle{};
            RHI::TextureDesc Desc{};
            bool InUse = false;
        };

        struct BufferEntry
        {
            RHI::BufferHandle Handle{};
            RHI::BufferDesc Desc{};
            bool InUse = false;
        };

        std::vector<TextureEntry> m_Textures{};
        std::vector<BufferEntry> m_Buffers{};
        std::uint32_t m_NextTextureSlot = 1;
        std::uint32_t m_NextBufferSlot = 1;
    };
}
