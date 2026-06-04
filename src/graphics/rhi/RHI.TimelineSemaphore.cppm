module;

#include <cstdint>

export module Extrinsic.RHI.TimelineSemaphore;

import Extrinsic.RHI.QueueAffinity;

export namespace Extrinsic::RHI
{
    class ITimelineSemaphore
    {
    public:
        virtual ~ITimelineSemaphore() = default;

        virtual void Signal(QueueAffinity queue, std::uint64_t value) = 0;
        virtual void Wait(QueueAffinity queue, std::uint64_t value) = 0;
    };
}
