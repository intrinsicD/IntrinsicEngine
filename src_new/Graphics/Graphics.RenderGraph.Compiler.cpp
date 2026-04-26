module;

#include <algorithm>
#include <cstdint>
#include <expected>
#include <stack>
#include <ranges>
#include <unordered_set>
#include <vector>

module Extrinsic.Graphics.RenderGraph:Compiler;

import Extrinsic.RHI.Handles;
import Extrinsic.Core.Error;

namespace Extrinsic::Graphics
{
    namespace
    {
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
            };
        }

        std::vector<ResourceState> textureStates(textures.size());
        std::vector<ResourceState> bufferStates(buffers.size());
        std::vector<ResourceLifetime> textureLifetimes(textures.size());
        std::vector<ResourceLifetime> bufferLifetimes(buffers.size());
        std::vector<std::vector<std::uint32_t>> adjacency(passCount);
        std::vector<std::vector<std::uint32_t>> reverseAdjacency(passCount);
        std::vector<std::uint32_t> indegree(passCount, 0u);
        std::unordered_set<std::uint64_t> dedup{};
        dedup.reserve(static_cast<std::size_t>(passCount) * 4u);

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

        return CompiledRenderGraph{
            .PassCount = livePassCount,
            .CulledPassCount = passCount - livePassCount,
            .ResourceCount = resourceCount,
            .EdgeCount = activeEdgeCount,
            .TopologicalOrder = std::move(order),
            .TopologicalLayerByPass = std::move(layerByPass),
            .TextureLifetimes = std::move(textureLifetimes),
            .BufferLifetimes = std::move(bufferLifetimes),
        };
    }
}
