module;

#include <cstdint>

export module Extrinsic.RHI.QueueAffinity;

export namespace Extrinsic::RHI
{
    enum class QueueAffinity : std::uint8_t
    {
        Graphics = 0,
        AsyncCompute,
        Transfer,
    };
}
