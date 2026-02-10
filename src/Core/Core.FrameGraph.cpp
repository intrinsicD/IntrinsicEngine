module;

#include <cassert>
#include <cstdint>
#include <vector>

module Core:FrameGraph.Impl;
import :FrameGraph;
import :Error;
import :Memory;
import :Logging;
import :Hash;
import :Tasks;

namespace Core
{
    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    FrameGraph::FrameGraph(Memory::ScopeStack& scope)
        : m_Scope(scope)
    {
        m_PassPool.reserve(64);
        m_ResourceStates.reserve(32);
        m_LabelStates.reserve(8);
    }

    // -------------------------------------------------------------------------
    // Reset (call at the start of each frame)
    // -------------------------------------------------------------------------

    void FrameGraph::Reset()
    {
        m_ActivePassCount = 0;
        m_ExecutionLayers.clear();
        m_ResourceStates.clear();
        m_LabelStates.clear();
        // Note: m_PassPool is kept at high-water mark and recycled.
        // ScopeStack::Reset() is the caller's responsibility (frame-level).
    }

    // -------------------------------------------------------------------------
    // Dependency Edge Insertion
    // -------------------------------------------------------------------------

    void FrameGraph::AddDependency(uint32_t producer, uint32_t consumer)
    {
        if (producer == consumer) return;
        if (producer >= m_ActivePassCount || consumer >= m_ActivePassCount) return;

        auto& prodNode = m_PassPool[producer];

        // Deduplicate (linear scan is fine for typical dependency counts < 10).
        for (uint32_t dep : prodNode.Dependents)
        {
            if (dep == consumer) return;
        }

        prodNode.Dependents.push_back(consumer);
        m_PassPool[consumer].Indegree++;
    }

    // -------------------------------------------------------------------------
    // Resource / Label State Lookup
    // -------------------------------------------------------------------------

    FrameGraph::ResourceState& FrameGraph::GetResourceState(size_t typeToken)
    {
        // Linear scan is fine for typical system counts (< 30 resource types).
        for (auto& [key, state] : m_ResourceStates)
        {
            if (key == typeToken) return state;
        }
        m_ResourceStates.emplace_back(typeToken, ResourceState{});
        return m_ResourceStates.back().second;
    }

    FrameGraph::ResourceState& FrameGraph::GetLabelState(Hash::StringID label)
    {
        for (auto& [key, state] : m_LabelStates)
        {
            if (key == label.Value) return state;
        }
        m_LabelStates.emplace_back(label.Value, ResourceState{});
        return m_LabelStates.back().second;
    }

    // -------------------------------------------------------------------------
    // Builder: WaitFor / Signal (non-template, so they live here)
    // -------------------------------------------------------------------------

    void FrameGraphBuilder::WaitFor(Hash::StringID label)
    {
        auto& state = m_Graph.GetLabelState(label);

        if (state.LastWriterIndex != FrameGraph::ResourceState::kInvalid)
        {
            m_Graph.AddDependency(state.LastWriterIndex, m_PassIndex);
        }
    }

    void FrameGraphBuilder::Signal(Hash::StringID label)
    {
        auto& state = m_Graph.GetLabelState(label);

        // If a previous pass also signaled this label, WAW ordering.
        if (state.LastWriterIndex != FrameGraph::ResourceState::kInvalid)
        {
            m_Graph.AddDependency(state.LastWriterIndex, m_PassIndex);
        }

        // All waiters on the previous signal must complete before this new signal.
        for (uint32_t readerIdx : state.CurrentReaders)
        {
            if (readerIdx != m_PassIndex)
            {
                m_Graph.AddDependency(readerIdx, m_PassIndex);
            }
        }

        state.CurrentReaders.clear();
        state.LastWriterIndex = m_PassIndex;
    }

    // -------------------------------------------------------------------------
    // Compile: Build DAG and Topological Sort into Layers
    // -------------------------------------------------------------------------

    Result FrameGraph::Compile()
    {
        m_ExecutionLayers.clear();
        if (m_ActivePassCount == 0) return Ok();

        // --- Kahn's Algorithm with Layer Grouping ---
        // We copy indegrees so we can decrement without mutating PassNodes.
        std::vector<uint32_t> indeg(m_ActivePassCount);
        for (uint32_t i = 0; i < m_ActivePassCount; ++i)
        {
            indeg[i] = m_PassPool[i].Indegree;
        }

        // Seed with root nodes (indegree == 0).
        std::vector<uint32_t> layer;
        layer.reserve(m_ActivePassCount);
        for (uint32_t i = 0; i < m_ActivePassCount; ++i)
        {
            if (indeg[i] == 0) layer.push_back(i);
        }

        uint32_t processed = 0;

        while (!layer.empty())
        {
            m_ExecutionLayers.push_back(layer);
            processed += static_cast<uint32_t>(layer.size());

            std::vector<uint32_t> next;
            next.reserve(m_ActivePassCount);

            for (uint32_t nodeIdx : layer)
            {
                for (uint32_t depIdx : m_PassPool[nodeIdx].Dependents)
                {
                    if (depIdx >= m_ActivePassCount) continue;
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

        if (processed != m_ActivePassCount)
        {
            Log::Error("FrameGraph: dependency cycle detected (processed {} / {})", processed, m_ActivePassCount);
            assert(false && "FrameGraph: dependency cycle detected — check Read/Write declarations");
            return Err(ErrorCode::InvalidState);
        }

        return Ok();
    }

    // -------------------------------------------------------------------------
    // Execute: Dispatch Layers to Task Scheduler
    // -------------------------------------------------------------------------

    void FrameGraph::Execute()
    {
        for (const auto& layer : m_ExecutionLayers)
        {
            if (layer.empty()) continue;

            if (layer.size() == 1)
            {
                // Single task in layer: run inline on this thread (avoid dispatch overhead).
                auto& pass = m_PassPool[layer[0]];
                pass.ExecuteFn(pass.ExecuteUserData);
            }
            else
            {
                // Dispatch all tasks in this layer to the scheduler.
                for (uint32_t nodeIdx : layer)
                {
                    auto& pass = m_PassPool[nodeIdx];
                    // Capture by value — thunk and pointer are trivially copyable.
                    auto thunk = pass.ExecuteFn;
                    auto* data = pass.ExecuteUserData;
                    Tasks::Scheduler::Dispatch([thunk, data]() { thunk(data); });
                }

                // Barrier: wait for this layer to finish before the next.
                Tasks::Scheduler::WaitForAll();
            }
        }
    }
}
