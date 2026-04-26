module;

#include <cstdint>
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
        for (const BarrierPacket& packet : graph.BarrierPackets)
        {
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

        for (const std::uint32_t passIndex : graph.TopologicalOrder)
        {
            if (onPass)
            {
                onPass(passIndex);
            }
        }

        return Core::Ok();
    }
}
