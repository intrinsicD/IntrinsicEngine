module;

#include <algorithm>
#include <cstdint>
#include <expected>
#include <stack>
#include <ranges>
#include <string>
#include <unordered_set>
#include <vector>

module Extrinsic.Graphics.RenderGraph:Compiler;

import Extrinsic.RHI.Handles;
import Extrinsic.Core.Error;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] constexpr TextureBarrierState ToTextureBarrierState(const TextureUsage usage)
        {
            switch (usage)
            {
            case TextureUsage::ColorAttachmentRead:
            case TextureUsage::ColorAttachmentWrite: return TextureBarrierState::ColorAttachmentWrite;
            case TextureUsage::DepthRead: return TextureBarrierState::DepthRead;
            case TextureUsage::DepthWrite: return TextureBarrierState::DepthWrite;
            case TextureUsage::ShaderRead: return TextureBarrierState::ShaderRead;
            case TextureUsage::ShaderWrite: return TextureBarrierState::ShaderWrite;
            case TextureUsage::TransferSrc: return TextureBarrierState::TransferSrc;
            case TextureUsage::TransferDst: return TextureBarrierState::TransferDst;
            case TextureUsage::Present: return TextureBarrierState::Present;
            }
            return TextureBarrierState::Undefined;
        }

        [[nodiscard]] constexpr TextureBarrierState ToTextureBarrierState(const TextureState state)
        {
            switch (state)
            {
            case TextureState::Undefined: return TextureBarrierState::Undefined;
            case TextureState::ShaderRead: return TextureBarrierState::ShaderRead;
            case TextureState::ShaderWrite: return TextureBarrierState::ShaderWrite;
            case TextureState::ColorAttachmentWrite: return TextureBarrierState::ColorAttachmentWrite;
            case TextureState::DepthWrite: return TextureBarrierState::DepthWrite;
            case TextureState::TransferSrc: return TextureBarrierState::TransferSrc;
            case TextureState::TransferDst: return TextureBarrierState::TransferDst;
            case TextureState::Present: return TextureBarrierState::Present;
            }
            return TextureBarrierState::Undefined;
        }

        [[nodiscard]] constexpr BufferBarrierState ToBufferBarrierState(const BufferUsage usage)
        {
            switch (usage)
            {
            case BufferUsage::IndirectRead: return BufferBarrierState::IndirectRead;
            case BufferUsage::IndexRead: return BufferBarrierState::IndexRead;
            case BufferUsage::VertexRead: return BufferBarrierState::VertexRead;
            case BufferUsage::ShaderRead: return BufferBarrierState::ShaderRead;
            case BufferUsage::ShaderWrite: return BufferBarrierState::ShaderWrite;
            case BufferUsage::TransferSrc: return BufferBarrierState::TransferSrc;
            case BufferUsage::TransferDst: return BufferBarrierState::TransferDst;
            case BufferUsage::HostReadback: return BufferBarrierState::HostReadback;
            }
            return BufferBarrierState::Undefined;
        }

        [[nodiscard]] constexpr BufferBarrierState ToBufferBarrierState(const BufferState state)
        {
            switch (state)
            {
            case BufferState::Undefined: return BufferBarrierState::Undefined;
            case BufferState::ShaderRead: return BufferBarrierState::ShaderRead;
            case BufferState::ShaderWrite: return BufferBarrierState::ShaderWrite;
            case BufferState::VertexRead: return BufferBarrierState::VertexRead;
            case BufferState::IndexRead: return BufferBarrierState::IndexRead;
            case BufferState::IndirectRead: return BufferBarrierState::IndirectRead;
            case BufferState::TransferSrc: return BufferBarrierState::TransferSrc;
            case BufferState::TransferDst: return BufferBarrierState::TransferDst;
            case BufferState::HostReadback: return BufferBarrierState::HostReadback;
            }
            return BufferBarrierState::Undefined;
        }

        struct ResourceState
        {
            std::int32_t LastWriter = -1;
            std::vector<std::uint32_t> Readers{};
        };

        constexpr std::uint64_t EncodeEdge(const std::uint32_t from, const std::uint32_t to)
        {
            return (static_cast<std::uint64_t>(from) << 32ull) | static_cast<std::uint64_t>(to);
        }

        bool AddEdge(const std::uint32_t from,
                     const std::uint32_t to,
                     std::vector<std::vector<std::uint32_t>>& adjacency,
                     std::vector<std::uint32_t>& indegree,
                     std::unordered_set<std::uint64_t>& dedup)
        {
            if (from == to)
            {
                return false;
            }
            const std::uint64_t key = EncodeEdge(from, to);
            if (!dedup.insert(key).second)
            {
                return false;
            }
            adjacency[from].push_back(to);
            ++indegree[to];
            return true;
        }

        void ProcessRead(const std::uint32_t passIndex,
                         ResourceState& state,
                         std::vector<std::vector<std::uint32_t>>& adjacency,
                         std::vector<std::uint32_t>& indegree,
                         std::unordered_set<std::uint64_t>& dedup)
        {
            if (state.LastWriter >= 0)
            {
                AddEdge(static_cast<std::uint32_t>(state.LastWriter), passIndex, adjacency, indegree, dedup);
            }
            state.Readers.push_back(passIndex);
        }

        void ProcessWrite(const std::uint32_t passIndex,
                          ResourceState& state,
                          std::vector<std::vector<std::uint32_t>>& adjacency,
                          std::vector<std::uint32_t>& indegree,
                          std::unordered_set<std::uint64_t>& dedup)
        {
            if (state.LastWriter >= 0)
            {
                AddEdge(static_cast<std::uint32_t>(state.LastWriter), passIndex, adjacency, indegree, dedup);
            }
            for (const std::uint32_t reader : state.Readers)
            {
                AddEdge(reader, passIndex, adjacency, indegree, dedup);
            }
            state.Readers.clear();
            state.LastWriter = static_cast<std::int32_t>(passIndex);
        }

        void UpdateLifetime(ResourceLifetime& lifetime, const std::uint32_t passIndex)
        {
            if (!lifetime.HasUse)
            {
                lifetime.HasUse = true;
                lifetime.FirstUsePass = passIndex;
                lifetime.LastUsePass = passIndex;
                return;
            }

            lifetime.FirstUsePass = std::min(lifetime.FirstUsePass, passIndex);
            lifetime.LastUsePass = std::max(lifetime.LastUsePass, passIndex);
        }
    }

    Core::Expected<CompiledRenderGraph> RenderGraphCompiler::Compile(
        const std::span<const RenderPassRecord> passes,
        const std::span<const TextureResourceDesc> textures,
        const std::span<const BufferResourceDesc> buffers)
    {
        const std::uint32_t passCount = static_cast<std::uint32_t>(passes.size());
        const std::uint32_t resourceCount = static_cast<std::uint32_t>(textures.size() + buffers.size());
        if (passCount == 0)
        {
            return CompiledRenderGraph{
                .PassCount = passCount,
                .ResourceCount = resourceCount,
                .TextureHandles = std::vector<RHI::TextureHandle>(textures.size()),
                .BufferHandles = std::vector<RHI::BufferHandle>(buffers.size()),
                .TextureImported = std::vector<bool>(textures.size(), false),
                .BufferImported = std::vector<bool>(buffers.size(), false),
            };
        }

        std::vector<ResourceState> textureStates(textures.size());
        std::vector<ResourceState> bufferStates(buffers.size());
        std::vector<RHI::TextureHandle> textureHandles(textures.size());
        std::vector<RHI::BufferHandle> bufferHandles(buffers.size());
        std::vector<bool> textureImported(textures.size(), false);
        std::vector<bool> bufferImported(buffers.size(), false);
        std::vector<ResourceLifetime> textureLifetimes(textures.size());
        std::vector<ResourceLifetime> bufferLifetimes(buffers.size());
        std::vector<std::vector<std::uint32_t>> adjacency(passCount);
        std::vector<std::vector<std::uint32_t>> reverseAdjacency(passCount);
        std::vector<std::uint32_t> indegree(passCount, 0u);
        std::unordered_set<std::uint64_t> dedup{};
        dedup.reserve(static_cast<std::size_t>(passCount) * 4u);

        for (std::uint32_t textureIndex = 0; textureIndex < textures.size(); ++textureIndex)
        {
            if (textures[textureIndex].Imported)
            {
                textureHandles[textureIndex] = textures[textureIndex].ImportedHandle;
                textureImported[textureIndex] = true;
            }
        }
        for (std::uint32_t bufferIndex = 0; bufferIndex < buffers.size(); ++bufferIndex)
        {
            if (buffers[bufferIndex].Imported)
            {
                bufferHandles[bufferIndex] = buffers[bufferIndex].ImportedHandle;
                bufferImported[bufferIndex] = true;
            }
        }

        for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            const RenderPassRecord& pass = passes[passIndex];
            for (const TextureAccess& access : pass.TextureAccesses)
            {
                if (access.Ref.Index >= textureStates.size())
                {
                    return std::unexpected(Core::ErrorCode::OutOfRange);
                }
                if (access.Write)
                {
                    ProcessWrite(passIndex, textureStates[access.Ref.Index], adjacency, indegree, dedup);
                }
                else
                {
                    ProcessRead(passIndex, textureStates[access.Ref.Index], adjacency, indegree, dedup);
                }
                UpdateLifetime(textureLifetimes[access.Ref.Index], passIndex);
            }

            for (const BufferAccess& access : pass.BufferAccesses)
            {
                if (access.Ref.Index >= bufferStates.size())
                {
                    return std::unexpected(Core::ErrorCode::OutOfRange);
                }
                if (access.Write)
                {
                    ProcessWrite(passIndex, bufferStates[access.Ref.Index], adjacency, indegree, dedup);
                }
                else
                {
                    ProcessRead(passIndex, bufferStates[access.Ref.Index], adjacency, indegree, dedup);
                }
                UpdateLifetime(bufferLifetimes[access.Ref.Index], passIndex);
            }

            const bool hasColorAttachmentWrite = std::ranges::any_of(
                pass.TextureAccesses, [](const TextureAccess& access) { return access.Usage == TextureUsage::ColorAttachmentWrite; });
            const bool hasDepthAccess = std::ranges::any_of(pass.TextureAccesses, [](const TextureAccess& access) {
                return access.Usage == TextureUsage::DepthRead || access.Usage == TextureUsage::DepthWrite;
            });

            if (pass.HasRenderPassDesc)
            {
                if (!pass.RenderPass.ColorTargets.empty() && !hasColorAttachmentWrite)
                {
                    return std::unexpected(Core::ErrorCode::InvalidArgument);
                }
                if (pass.RenderPass.Depth.Target.IsValid() && !hasDepthAccess)
                {
                    return std::unexpected(Core::ErrorCode::InvalidArgument);
                }
            }
        }

        for (std::uint32_t from = 0; from < passCount; ++from)
        {
            for (const std::uint32_t to : adjacency[from])
            {
                reverseAdjacency[to].push_back(from);
            }
        }

        std::vector<bool> live(passCount, false);
        std::stack<std::uint32_t> rootStack{};
        for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            const RenderPassRecord& pass = passes[passIndex];
            const bool hasPresentUse = std::ranges::any_of(
                pass.TextureAccesses, [](const TextureAccess& access) { return access.Usage == TextureUsage::Present; });
            const bool writesImportedTexture = std::ranges::any_of(pass.TextureAccesses, [textures](const TextureAccess& access) {
                return access.Write && access.Ref.Index < textures.size() && textures[access.Ref.Index].Imported;
            });
            const bool writesImportedBuffer = std::ranges::any_of(pass.BufferAccesses, [buffers](const BufferAccess& access) {
                return access.Write && access.Ref.Index < buffers.size() && buffers[access.Ref.Index].Imported;
            });

            if (pass.SideEffect || hasPresentUse || writesImportedTexture || writesImportedBuffer)
            {
                rootStack.push(passIndex);
            }
        }

        while (!rootStack.empty())
        {
            const std::uint32_t node = rootStack.top();
            rootStack.pop();
            if (live[node])
            {
                continue;
            }
            live[node] = true;
            for (const std::uint32_t predecessor : reverseAdjacency[node])
            {
                rootStack.push(predecessor);
            }
        }

        std::vector<std::uint32_t> liveIndegree = indegree;
        std::uint32_t livePassCount = 0;
        for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            if (live[passIndex])
            {
                ++livePassCount;
                continue;
            }

            for (const std::uint32_t succ : adjacency[passIndex])
            {
                if (live[succ] && liveIndegree[succ] > 0)
                {
                    --liveIndegree[succ];
                }
            }
        }

        std::vector<std::uint32_t> ready{};
        ready.reserve(livePassCount);
        for (std::uint32_t i = 0; i < passCount; ++i)
        {
            if (live[i] && liveIndegree[i] == 0)
            {
                ready.push_back(i);
            }
        }
        std::ranges::sort(ready);

        std::vector<std::uint32_t> order{};
        order.reserve(livePassCount);
        std::vector<std::uint32_t> layerByPass(passCount, 0u);
        std::vector<std::uint32_t> layerMaxPred(passCount, 0u);

        while (!ready.empty())
        {
            const std::uint32_t node = ready.front();
            ready.erase(ready.begin());
            order.push_back(node);

            std::vector<std::uint32_t> newlyReady{};
            for (const std::uint32_t succ : adjacency[node])
            {
                if (!live[succ])
                {
                    continue;
                }
                layerMaxPred[succ] = std::max(layerMaxPred[succ], layerByPass[node] + 1u);
                if (--liveIndegree[succ] == 0)
                {
                    layerByPass[succ] = layerMaxPred[succ];
                    newlyReady.push_back(succ);
                }
            }
            if (!newlyReady.empty())
            {
                std::ranges::sort(newlyReady);
                ready.insert(ready.end(), newlyReady.begin(), newlyReady.end());
            }
        }

        if (order.size() != livePassCount)
        {
            CompiledRenderGraph failed{
                .PassCount = livePassCount,
                .CulledPassCount = passCount - livePassCount,
                .ResourceCount = resourceCount,
                .EdgeCount = static_cast<std::uint32_t>(dedup.size()),
                .TopologicalOrder = order,
                .TopologicalLayerByPass = layerByPass,
                .TextureLifetimes = std::move(textureLifetimes),
                .BufferLifetimes = std::move(bufferLifetimes),
            };
            failed.Diagnostic = "RenderGraph cycle detected.";
            return std::unexpected(Core::ErrorCode::InvalidState);
        }

        std::uint32_t activeEdgeCount = 0;
        for (std::uint32_t from = 0; from < passCount; ++from)
        {
            if (!live[from])
            {
                continue;
            }
            for (const std::uint32_t to : adjacency[from])
            {
                if (live[to])
                {
                    ++activeEdgeCount;
                }
            }
        }

        std::vector<BarrierPacket> barrierPackets{};
        barrierPackets.reserve(order.size());
        std::vector<std::string> passNames{};
        passNames.reserve(order.size());
        std::vector<TextureBarrierState> textureStateByRef(textures.size(), TextureBarrierState::Undefined);
        std::vector<BufferBarrierState> bufferStateByRef(buffers.size(), BufferBarrierState::Undefined);

        for (std::uint32_t i = 0; i < textures.size(); ++i)
        {
            if (textures[i].Imported)
            {
                textureStateByRef[i] = ToTextureBarrierState(textures[i].InitialState);
            }
        }
        for (std::uint32_t i = 0; i < buffers.size(); ++i)
        {
            if (buffers[i].Imported)
            {
                bufferStateByRef[i] = ToBufferBarrierState(buffers[i].InitialState);
            }
        }

        for (const std::uint32_t passIndex : order)
        {
            const auto& pass = passes[passIndex];
            passNames.push_back(pass.Name);
            BarrierPacket packet{};
            packet.PassIndex = passIndex;

            for (const auto& access : pass.TextureAccesses)
            {
                const auto next = ToTextureBarrierState(access.Usage);
                const auto prev = textureStateByRef[access.Ref.Index];
                if (prev != next)
                {
                    packet.TextureBarriers.push_back(TextureBarrierPacket{
                        .TextureIndex = access.Ref.Index,
                        .Before = prev,
                        .After = next,
                    });
                    textureStateByRef[access.Ref.Index] = next;
                }
            }

            for (const auto& access : pass.BufferAccesses)
            {
                const auto next = ToBufferBarrierState(access.Usage);
                const auto prev = bufferStateByRef[access.Ref.Index];
                if (prev != next)
                {
                    packet.BufferBarriers.push_back(BufferBarrierPacket{
                        .BufferIndex = access.Ref.Index,
                        .Before = prev,
                        .After = next,
                    });
                    bufferStateByRef[access.Ref.Index] = next;
                }
            }

            if (!packet.TextureBarriers.empty() || !packet.BufferBarriers.empty())
            {
                barrierPackets.push_back(std::move(packet));
            }
        }

        return CompiledRenderGraph{
            .PassCount = livePassCount,
            .CulledPassCount = passCount - livePassCount,
            .ResourceCount = resourceCount,
            .EdgeCount = activeEdgeCount,
            .TopologicalOrder = std::move(order),
            .TopologicalLayerByPass = std::move(layerByPass),
            .PassNames = std::move(passNames),
            .TextureLifetimes = std::move(textureLifetimes),
            .BufferLifetimes = std::move(bufferLifetimes),
            .TextureHandles = std::move(textureHandles),
            .BufferHandles = std::move(bufferHandles),
            .TextureImported = std::move(textureImported),
            .BufferImported = std::move(bufferImported),
            .BarrierPackets = std::move(barrierPackets),
        };
    }
}
