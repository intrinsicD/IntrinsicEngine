module;

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

module Extrinsic.Graphics.RenderGraph;

import :Barriers;
import :Executor;

import Extrinsic.Core.Error;
import Extrinsic.Core.Tasks;
import Extrinsic.Core.Tasks.CounterEvent;
import :Compiler;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] Core::Result ValidateExecutableGraph(const CompiledRenderGraph& graph)
        {
            if (!AreBarrierPacketsSortedByPassAndStage(graph.BarrierPackets))
            {
                return Core::Err(Core::ErrorCode::InvalidState);
            }

            Core::Result boundsResult = ValidateBarrierPacketBounds(graph);
            if (!boundsResult.has_value())
            {
                return boundsResult;
            }

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
            }

            return Core::Ok();
        }

        [[nodiscard]] Core::Expected<std::vector<std::vector<std::uint32_t>>> BuildParallelRecordLayers(
            const CompiledRenderGraph& graph)
        {
            std::uint32_t maxLayer = 0u;
            bool hasPass = false;
            for (const std::uint32_t passIndex : graph.TopologicalOrder)
            {
                if (passIndex >= graph.TopologicalLayerByPass.size())
                {
                    return Core::Err<std::vector<std::vector<std::uint32_t>>>(Core::ErrorCode::OutOfRange);
                }
                maxLayer = std::max(maxLayer, graph.TopologicalLayerByPass[passIndex]);
                hasPass = true;
            }

            std::vector<std::vector<std::uint32_t>> layers{};
            if (hasPass)
            {
                layers.resize(static_cast<std::size_t>(maxLayer) + 1u);
            }

            for (const std::uint32_t passIndex : graph.TopologicalOrder)
            {
                layers[graph.TopologicalLayerByPass[passIndex]].push_back(passIndex);
            }

            return layers;
        }

        [[nodiscard]] Core::Result EmitBarriersForPass(const CompiledRenderGraph& graph,
                                                       const std::uint32_t passIndex,
                                                       const BarrierPacketStage stage,
                                                       const RenderGraphExecutor::BarrierObserver& onBarriers)
        {
            const BarrierPacketRange range = FindBarrierPacketRange(graph.BarrierPackets, passIndex, stage);
            for (std::size_t packetIndex = range.Begin; packetIndex < range.End; ++packetIndex)
            {
                const BarrierPacket& packet = graph.BarrierPackets[packetIndex];
                if (onBarriers)
                {
                    onBarriers(packet);
                }
            }

            return Core::Ok();
        }

        struct SignalCounterOnExit
        {
            Core::Tasks::CounterEvent& Counter;

            explicit SignalCounterOnExit(Core::Tasks::CounterEvent& counter)
                : Counter(counter)
            {
            }

            ~SignalCounterOnExit()
            {
                Counter.Signal();
            }

            SignalCounterOnExit(const SignalCounterOnExit&) = delete;
            SignalCounterOnExit& operator=(const SignalCounterOnExit&) = delete;
        };
    }

    Core::Result ValidateBarrierPacketBounds(const CompiledRenderGraph& graph)
    {
        const std::uint32_t finalPassIndex = static_cast<std::uint32_t>(graph.PassDeclarations.size());
        for (const BarrierPacket& packet : graph.BarrierPackets)
        {
            if (packet.PassIndex > finalPassIndex)
            {
                return Core::Err(Core::ErrorCode::OutOfRange);
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

            for (const TextureAliasReuseBarrierPacket& aliasBarrier : packet.TextureAliasReuseBarriers)
            {
                if (aliasBarrier.PreviousTextureIndex >= graph.TextureHandles.size() ||
                    aliasBarrier.TextureIndex >= graph.TextureHandles.size())
                {
                    return Core::Err(Core::ErrorCode::OutOfRange);
                }
            }

            for (const BufferAliasReuseBarrierPacket& aliasBarrier : packet.BufferAliasReuseBarriers)
            {
                if (aliasBarrier.PreviousBufferIndex >= graph.BufferHandles.size() ||
                    aliasBarrier.BufferIndex >= graph.BufferHandles.size())
                {
                    return Core::Err(Core::ErrorCode::OutOfRange);
                }
            }
        }

        return Core::Ok();
    }

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
        Core::Result validationResult = ValidateExecutableGraph(graph);
        if (!validationResult.has_value())
        {
            return validationResult;
        }

        for (const std::uint32_t passIndex : graph.TopologicalOrder)
        {
            const CompiledPassDeclarations& declarations = graph.PassDeclarations[passIndex];

            if (onResolve)
            {
                Core::Result resolveResult = onResolve(declarations);
                if (!resolveResult.has_value())
                {
                    return resolveResult;
                }
            }

            Core::Result barrierResult =
                EmitBarriersForPass(graph, passIndex, BarrierPacketStage::BeforePass, onBarriers);
            if (!barrierResult.has_value())
            {
                return barrierResult;
            }

            if (onPass)
            {
                onPass(passIndex);
            }

            Core::Result postBarrierResult =
                EmitBarriersForPass(graph, passIndex, BarrierPacketStage::AfterPass, onBarriers);
            if (!postBarrierResult.has_value())
            {
                return postBarrierResult;
            }
        }

        Core::Result finalBarrierResult =
            EmitBarriersForPass(graph, finalPassIndex, BarrierPacketStage::BeforePass, onBarriers);
        if (!finalBarrierResult.has_value())
        {
            return finalBarrierResult;
        }

        return Core::Ok();
    }

    Core::Result RenderGraphExecutor::ExecuteParallelRecordJoin(
        const CompiledRenderGraph& graph,
        ParallelRecordObserver onRecord,
        PassObserver onSubmit,
        BarrierObserver onBarriers,
        ParallelRecordStats* stats,
        ParallelRecordOptions options) const
    {
        if (stats)
        {
            *stats = {};
        }

        const std::uint32_t finalPassIndex = static_cast<std::uint32_t>(graph.PassDeclarations.size());
        Core::Result validationResult = ValidateExecutableGraph(graph);
        if (!validationResult.has_value())
        {
            return validationResult;
        }

        auto layers = BuildParallelRecordLayers(graph);
        if (!layers.has_value())
        {
            return Core::Err(layers.error());
        }

        if (stats)
        {
            stats->LayerCount = static_cast<std::uint32_t>(layers->size());
            for (const std::vector<std::uint32_t>& layer : *layers)
            {
                stats->MaxLayerWidth = std::max(stats->MaxLayerWidth,
                                                static_cast<std::uint32_t>(layer.size()));
                stats->ScheduledPassCount += static_cast<std::uint32_t>(layer.size());
            }
        }

        const std::uint32_t minWorkerPassCount =
            options.MinWorkerPassCount == 0u ? 1u : options.MinWorkerPassCount;
        const bool schedulerReady = options.UseScheduler && Core::Tasks::Scheduler::IsInitialized();
        const auto recordOne = [&onRecord](const std::uint32_t passIndex,
                                           const std::uint32_t layerIndex) -> Core::Result
        {
            return onRecord ? onRecord(passIndex, layerIndex) : Core::Ok();
        };

        for (std::uint32_t layerIndex = 0u; layerIndex < layers->size(); ++layerIndex)
        {
            const std::vector<std::uint32_t>& layer = (*layers)[layerIndex];
            if (layer.empty())
            {
                continue;
            }

            const bool useWorkers = schedulerReady &&
                                    layer.size() >= static_cast<std::size_t>(minWorkerPassCount);
            if (!useWorkers)
            {
                if (stats)
                {
                    stats->CallerRecordCount += static_cast<std::uint32_t>(layer.size());
                }
                for (const std::uint32_t passIndex : layer)
                {
                    Core::Result recordResult = recordOne(passIndex, layerIndex);
                    if (!recordResult.has_value())
                    {
                        return recordResult;
                    }
                }
                continue;
            }

            if (stats)
            {
                stats->UsedScheduler = true;
                stats->WorkerTaskCount += static_cast<std::uint32_t>(layer.size());
            }

            Core::Tasks::CounterEvent done{static_cast<std::uint32_t>(layer.size())};
            std::atomic<std::uint32_t> firstError{0u};
            for (const std::uint32_t passIndex : layer)
            {
                Core::Tasks::Scheduler::Dispatch([passIndex, layerIndex, &done, &firstError, &recordOne]()
                {
                    SignalCounterOnExit signalDone{done};
                    Core::Result recordResult = recordOne(passIndex, layerIndex);
                    if (!recordResult.has_value())
                    {
                        std::uint32_t expected = 0u;
                        (void)firstError.compare_exchange_strong(
                            expected,
                            static_cast<std::uint32_t>(recordResult.error()),
                            std::memory_order_acq_rel,
                            std::memory_order_acquire);
                    }
                });
            }

            while (!done.IsReady())
            {
                std::this_thread::yield();
            }

            const std::uint32_t errorCode = firstError.load(std::memory_order_acquire);
            if (errorCode != 0u)
            {
                return Core::Err(static_cast<Core::ErrorCode>(errorCode));
            }
        }

        for (const std::uint32_t passIndex : graph.TopologicalOrder)
        {
            Core::Result barrierResult =
                EmitBarriersForPass(graph, passIndex, BarrierPacketStage::BeforePass, onBarriers);
            if (!barrierResult.has_value())
            {
                return barrierResult;
            }

            if (onSubmit)
            {
                onSubmit(passIndex);
            }

            Core::Result postBarrierResult =
                EmitBarriersForPass(graph, passIndex, BarrierPacketStage::AfterPass, onBarriers);
            if (!postBarrierResult.has_value())
            {
                return postBarrierResult;
            }
        }

        Core::Result finalBarrierResult =
            EmitBarriersForPass(graph, finalPassIndex, BarrierPacketStage::BeforePass, onBarriers);
        if (!finalBarrierResult.has_value())
        {
            return finalBarrierResult;
        }

        return Core::Ok();
    }
}
