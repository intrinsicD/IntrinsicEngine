module;

#include <cstdint>

export module Extrinsic.Graphics.RenderGraph:TransientAllocator;

namespace Extrinsic::Graphics
{
    export class TransientAllocator final
    {
    public:
        [[nodiscard]] std::uint32_t AcquireSlot();
        void Reset();

    private:
        std::uint32_t m_NextSlot = 0;
    };
}
