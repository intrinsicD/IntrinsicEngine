module;

#include <cstdint>

export module Extrinsic.Core.Dag.Scheduler:Policy;

import :Types;

export namespace Extrinsic::Core::Dag
{
    struct ReadyNodePolicy
    {
        TaskPriority priority = TaskPriority::Normal;
        uint32_t criticalLevel = 0;
        uint32_t insertionOrder = 0;
    };
}
