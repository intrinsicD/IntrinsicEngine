module;

#include <cstdint>
#include <utility>
#include <vector>

module Extrinsic.Graphics.RenderGraph;

import :Executor;

import Extrinsic.Core.Error;
import :Compiler;

namespace Extrinsic::Graphics
{
    Core::Result RenderGraphExecutor::Execute(const CompiledRenderGraph& graph,
                                              PassObserver onPass,
                                              BarrierObserver onBarriers) const
    {
        return Execute(graph, {}, std::move(onPass), std::move(onBarriers));
    }

    Core::Result RenderGraphExecutor::Execute(const CompiledRenderGraph& graph,
                                              ResolveObserver onResolve,
                                              PassObserver onPass,
                                              BarrierObserver onBarriers) const
    {
        const std::uint32_t finalPassIndex = static_cast<std::uint32_t>(graph.PassDeclarations.size());

        for (const BarrierPacket& packet : graph.BarrierPackets)
        {
            if (packet.PassIndex > finalPassIndex)
            {
                return Core::Err(Core::ErrorCode::OutOfRange);
            }
        }

        const auto emitBarriersForPass = [&](const std::uint32_t passIndex) -> Core::Result {
            for (const BarrierPacket& packet : graph.BarrierPackets)
            {
                if (packet.PassIndex != passIndex)
                {
                    continue;
                }

                for (const TextureBarrierPacket& textureBarrier : packet.TextureBarriers)
                {
                    if (textureBarrier.TextureIndex >= graph.TextureHandles.size())
                    {
                        return Core::Err(Core::ErrorCode::OutOfRange);
                    }
                }

                for (const BufferBarrierPacket& bufferBarrier : packet.BufferBarriers)
                {
                    if (bufferBarrier.BufferIndex >= graph.BufferHandles.size())
                    {
                        return Core::Err(Core::ErrorCode::OutOfRange);
                    }
                }

                if (onBarriers)
                {
                    onBarriers(packet);
                }
            }

            return Core::Ok();
        };

        for (const std::uint32_t passIndex : graph.TopologicalOrder)
        {
            if (passIndex >= graph.PassDeclarations.size())
            {
                return Core::Err(Core::ErrorCode::OutOfRange);
            }

            const CompiledPassDeclarations& declarations = graph.PassDeclarations[passIndex];
            if (declarations.PassIndex != passIndex)
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }

            if (onResolve)
            {
                Core::Result resolveResult = onResolve(declarations);
                if (!resolveResult.has_value())
                {
                    return resolveResult;
                }
            }

            Core::Result barrierResult = emitBarriersForPass(passIndex);
            if (!barrierResult.has_value())
            {
                return barrierResult;
            }

            if (onPass)
            {
                onPass(passIndex);
            }
        }

        Core::Result finalBarrierResult = emitBarriersForPass(finalPassIndex);
        if (!finalBarrierResult.has_value())
        {
            return finalBarrierResult;
        }

        return Core::Ok();
    }
}
