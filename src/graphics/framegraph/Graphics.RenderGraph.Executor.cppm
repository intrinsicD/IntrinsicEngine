module;

#include <cstdint>
#include <functional>

export module Extrinsic.Graphics.RenderGraph:Executor;

import Extrinsic.Core.Error;
import :Barriers;
import :Compiler;

namespace Extrinsic::Graphics
{
    export [[nodiscard]] Core::Result ValidateBarrierPacketBounds(const CompiledRenderGraph& graph);

    export struct ParallelRecordOptions
    {
        bool UseScheduler = true;
        std::uint32_t MinWorkerPassCount = 2u;
    };

    export struct ParallelRecordStats
    {
        std::uint32_t LayerCount = 0u;
        std::uint32_t MaxLayerWidth = 0u;
        std::uint32_t ScheduledPassCount = 0u;
        std::uint32_t WorkerTaskCount = 0u;
        std::uint32_t CallerRecordCount = 0u;
        bool UsedScheduler = false;
    };

    export class RenderGraphExecutor final
    {
    public:
        using BarrierObserver = std::function<void(const BarrierPacket&)>;
        using PassObserver = std::function<void(std::uint32_t passIndex)>;
        using ParallelRecordObserver = std::function<Core::Result(std::uint32_t passIndex,
                                                                   std::uint32_t layerIndex)>;
        using ResolveObserver = std::function<Core::Result(const CompiledPassDeclarations& declarations)>;

        [[nodiscard]] Core::Result Execute(const CompiledRenderGraph& graph,
                                           PassObserver onPass = {},
                                           BarrierObserver onBarriers = {}) const;
        [[nodiscard]] Core::Result Execute(const CompiledRenderGraph& graph,
                                           ResolveObserver onResolve,
                                           PassObserver onPass,
                                           BarrierObserver onBarriers) const;
        [[nodiscard]] Core::Result ExecuteParallelRecordJoin(
            const CompiledRenderGraph& graph,
            ParallelRecordObserver onRecord,
            PassObserver onSubmit = {},
            BarrierObserver onBarriers = {},
            ParallelRecordStats* stats = nullptr,
            ParallelRecordOptions options = {}) const;
    };
}
