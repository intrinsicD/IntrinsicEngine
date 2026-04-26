module;

#include <cstdint>
#include <functional>

export module Extrinsic.Graphics.RenderGraph:Executor;

import Extrinsic.Core.Error;
import :Compiler;

namespace Extrinsic::Graphics
{
    export class RenderGraphExecutor final
    {
    public:
        using BarrierObserver = std::function<void(const BarrierPacket&)>;
        using PassObserver = std::function<void(std::uint32_t passIndex)>;

        [[nodiscard]] Core::Result Execute(const CompiledRenderGraph& graph,
                                           PassObserver onPass = {},
                                           BarrierObserver onBarriers = {}) const;
    };
}
