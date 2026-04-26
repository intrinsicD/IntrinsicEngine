module;

#include <algorithm>
#include <cstdint>

module Extrinsic.Graphics.RenderGraph;

import :TransientAllocator;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] bool TextureDescCompatible(const RHI::TextureDesc& a, const RHI::TextureDesc& b)
        {
            return a.Width == b.Width && a.Height == b.Height && a.DepthOrArrayLayers == b.DepthOrArrayLayers &&
                   a.MipLevels == b.MipLevels && a.Fmt == b.Fmt && a.Dimension == b.Dimension &&
                   a.Usage == b.Usage && a.SampleCount == b.SampleCount;
        }

        [[nodiscard]] bool BufferDescCompatible(const RHI::BufferDesc& a, const RHI::BufferDesc& b)
        {
            return a.SizeBytes == b.SizeBytes && a.Usage == b.Usage && a.HostVisible == b.HostVisible;
        }
    }

    RHI::TextureHandle TransientAllocator::AcquireTexture(const RHI::TextureDesc& desc)
    {
        auto it = std::ranges::find_if(m_Textures, [&desc](const TextureEntry& entry) {
            return !entry.InUse && TextureDescCompatible(entry.Desc, desc);
        });
        if (it != m_Textures.end())
        {
            it->InUse = true;
            return it->Handle;
        }

        const auto slot = m_NextTextureSlot++;
        TextureEntry& entry = m_Textures.emplace_back();
        entry.Handle = RHI::TextureHandle{slot, 1u};
        entry.Desc = desc;
        entry.InUse = true;
        return entry.Handle;
    }

    RHI::BufferHandle TransientAllocator::AcquireBuffer(const RHI::BufferDesc& desc)
    {
        auto it = std::ranges::find_if(m_Buffers, [&desc](const BufferEntry& entry) {
            return !entry.InUse && BufferDescCompatible(entry.Desc, desc);
        });
        if (it != m_Buffers.end())
        {
            it->InUse = true;
            return it->Handle;
        }

        const auto slot = m_NextBufferSlot++;
        BufferEntry& entry = m_Buffers.emplace_back();
        entry.Handle = RHI::BufferHandle{slot, 1u};
        entry.Desc = desc;
        entry.InUse = true;
        return entry.Handle;
    }

    void TransientAllocator::ReleaseTexture(const RHI::TextureHandle handle)
    {
        auto it = std::ranges::find_if(m_Textures, [handle](const TextureEntry& entry) { return entry.Handle == handle; });
        if (it != m_Textures.end())
        {
            it->InUse = false;
        }
    }

    void TransientAllocator::ReleaseBuffer(const RHI::BufferHandle handle)
    {
        auto it = std::ranges::find_if(m_Buffers, [handle](const BufferEntry& entry) { return entry.Handle == handle; });
        if (it != m_Buffers.end())
        {
            it->InUse = false;
        }
    }

    void TransientAllocator::ResetFrame()
    {
        for (TextureEntry& entry : m_Textures)
        {
            entry.InUse = false;
        }
        for (BufferEntry& entry : m_Buffers)
        {
            entry.InUse = false;
        }
    }
}
