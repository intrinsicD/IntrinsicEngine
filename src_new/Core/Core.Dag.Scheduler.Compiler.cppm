module;

#include <vector>
#include <cstdint>

export module Extrinsic.Core.Dag.Scheduler:Compiler;

import :Types;

export namespace Extrinsic::Core::Dag
{
    struct CompiledGraph
    {
        std::vector<PlanTask> tasks{};
        uint32_t layerCount = 0;
    };
}
