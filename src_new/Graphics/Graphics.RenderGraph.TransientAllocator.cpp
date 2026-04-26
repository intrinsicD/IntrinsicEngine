module;

#include <cstdint>

module Extrinsic.Graphics.RenderGraph;

import :TransientAllocator;

namespace Extrinsic::Graphics
{
    std::uint32_t TransientAllocator::AcquireSlot()
    {
        return m_NextSlot++;
    }

    void TransientAllocator::Reset()
    {
        m_NextSlot = 0;
    }
}
