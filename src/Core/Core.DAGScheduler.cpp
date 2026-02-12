module;

#include <cassert>
#include <cstdint>
#include <vector>

module Core:DAGScheduler.Impl;
import :DAGScheduler;
import :Error;
import :Logging;

namespace Core
{
    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    DAGScheduler::DAGScheduler()
    {
        m_NodePool.reserve(64);
        m_ResourceStates.reserve(32);
    }

    // -------------------------------------------------------------------------
    // Reset (call at the start of each frame)
    // -------------------------------------------------------------------------

    void DAGScheduler::Reset()
    {
        m_ActiveNodeCount = 0;
        m_ExecutionLayers.clear();
        m_ResourceStates.clear();
        // Note: m_NodePool is kept at high-water mark and recycled.
    }

    // -------------------------------------------------------------------------
    // Node Management
    // -------------------------------------------------------------------------

    uint32_t DAGScheduler::AddNode()
    {
        uint32_t index = m_ActiveNodeCount;

        if (index >= m_NodePool.size())
        {
            m_NodePool.emplace_back();
        }

        auto& node = m_NodePool[index];
        node.Dependents.clear();
        node.Indegree = 0;

        ++m_ActiveNodeCount;
        return index;
    }

    // -------------------------------------------------------------------------
    // Resource State Lookup
    // -------------------------------------------------------------------------

    DAGScheduler::ResourceState& DAGScheduler::GetResourceState(size_t key)
    {
        // Linear scan is fine for typical counts (< 30 resource types).
        for (auto& [k, state] : m_ResourceStates)
        {
            if (k == key) return state;
        }
        m_ResourceStates.emplace_back(key, ResourceState{});
        return m_ResourceStates.back().second;
    }

    // -------------------------------------------------------------------------
    // Edge Insertion (deduplicated)
    // -------------------------------------------------------------------------

    void DAGScheduler::AddEdgeInternal(uint32_t producer, uint32_t consumer)
    {
        if (producer == consumer) return;
        if (producer >= m_ActiveNodeCount || consumer >= m_ActiveNodeCount) return;

        auto& prodNode = m_NodePool[producer];

        // Deduplicate (linear scan is fine for typical dependency counts < 10).
        for (uint32_t dep : prodNode.Dependents)
        {
            if (dep == consumer) return;
        }

        prodNode.Dependents.push_back(consumer);
        m_NodePool[consumer].Indegree++;
    }

    void DAGScheduler::AddEdge(uint32_t producer, uint32_t consumer)
    {
        AddEdgeInternal(producer, consumer);
    }

    // -------------------------------------------------------------------------
    // Hazard-Based Edge Building
    // -------------------------------------------------------------------------

    void DAGScheduler::DeclareRead(uint32_t nodeIndex, size_t resourceKey)
    {
        auto& state = GetResourceState(resourceKey);

        // RAW: If someone wrote this resource, depend on the last writer.
        if (state.LastWriterIndex != kInvalid)
        {
            AddEdgeInternal(state.LastWriterIndex, nodeIndex);
        }

        state.CurrentReaders.push_back(nodeIndex);
    }

    void DAGScheduler::DeclareWeakRead(uint32_t nodeIndex, size_t resourceKey)
    {
        auto& state = GetResourceState(resourceKey);

        // RAW: If someone wrote this resource, depend on the last writer.
        if (state.LastWriterIndex != kInvalid)
        {
            AddEdgeInternal(state.LastWriterIndex, nodeIndex);
        }

        // Unlike DeclareRead, do NOT register as reader.
        // Future writers won't need to wait for this node.
    }

    void DAGScheduler::DeclareWrite(uint32_t nodeIndex, size_t resourceKey)
    {
        auto& state = GetResourceState(resourceKey);

        // WAW: Depend on last writer.
        if (state.LastWriterIndex != kInvalid)
        {
            AddEdgeInternal(state.LastWriterIndex, nodeIndex);
        }

        // WAR: Depend on all current readers (they must finish before we overwrite).
        for (uint32_t readerIdx : state.CurrentReaders)
        {
            if (readerIdx != nodeIndex)
            {
                AddEdgeInternal(readerIdx, nodeIndex);
            }
        }

        // We become the exclusive owner.
        state.CurrentReaders.clear();
        state.LastWriterIndex = nodeIndex;
    }

    // -------------------------------------------------------------------------
    // Compile: Kahn's Algorithm with Layer Grouping
    // -------------------------------------------------------------------------

    Result DAGScheduler::Compile()
    {
        m_ExecutionLayers.clear();
        if (m_ActiveNodeCount == 0) return Ok();

        // Copy indegrees so we can decrement without mutating NodeData.
        std::vector<uint32_t> indeg(m_ActiveNodeCount);
        for (uint32_t i = 0; i < m_ActiveNodeCount; ++i)
        {
            indeg[i] = m_NodePool[i].Indegree;
        }

        // Seed with root nodes (indegree == 0).
        std::vector<uint32_t> layer;
        layer.reserve(m_ActiveNodeCount);
        for (uint32_t i = 0; i < m_ActiveNodeCount; ++i)
        {
            if (indeg[i] == 0) layer.push_back(i);
        }

        uint32_t processed = 0;

        while (!layer.empty())
        {
            m_ExecutionLayers.push_back(layer);
            processed += static_cast<uint32_t>(layer.size());

            std::vector<uint32_t> next;
            next.reserve(m_ActiveNodeCount);

            for (uint32_t nodeIdx : layer)
            {
                for (uint32_t depIdx : m_NodePool[nodeIdx].Dependents)
                {
                    if (depIdx >= m_ActiveNodeCount) continue;
                    if (indeg[depIdx] == 0) continue; // Already scheduled
                    indeg[depIdx]--;
                    if (indeg[depIdx] == 0)
                    {
                        next.push_back(depIdx);
                    }
                }
            }

            layer = std::move(next);
        }

        if (processed != m_ActiveNodeCount)
        {
            Log::Error("DAGScheduler: dependency cycle detected (processed {} / {})", processed, m_ActiveNodeCount);
            assert(false && "DAGScheduler: dependency cycle detected");
            return Err(ErrorCode::InvalidState);
        }

        return Ok();
    }
}
