module;

#include <cassert>
#include <cstdint>
#include <vector>

module Core.FrameGraph;
import Core.Error;
import Core.Memory;
import Core.Logging;
import Core.Hash;
import Core.Tasks;
import Core.DAGScheduler;

namespace Core
{
    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    FrameGraph::FrameGraph(Memory::ScopeStack& scope)
        : m_Scope(scope)
    {
        m_PassPool.reserve(64);
    }

    // -------------------------------------------------------------------------
    // Reset (call at the start of each frame)
    // -------------------------------------------------------------------------

    void FrameGraph::Reset()
    {
        m_Scheduler.Reset();
        // Note: m_PassPool is kept at high-water mark and recycled.
        // ScopeStack::Reset() is the caller's responsibility (frame-level).
    }

    // -------------------------------------------------------------------------
    // Builder: WaitFor / Signal (non-template, so they live here)
    // -------------------------------------------------------------------------

    void FrameGraphBuilder::WaitFor(Hash::StringID label)
    {
        // WaitFor is a weak read: depend on last signaler but don't register
        // as reader (future signalers don't need to wait for us).
        size_t key = FrameGraph::kLabelTag | static_cast<size_t>(label.Value);
        m_Graph.m_Scheduler.DeclareWeakRead(m_PassIndex, key);
    }

    void FrameGraphBuilder::Signal(Hash::StringID label)
    {
        // Signal is a write: WAW with prior signalers, WAR with pending waiters.
        size_t key = FrameGraph::kLabelTag | static_cast<size_t>(label.Value);
        m_Graph.m_Scheduler.DeclareWrite(m_PassIndex, key);
    }

    // -------------------------------------------------------------------------
    // Compile: Delegate to DAGScheduler
    // -------------------------------------------------------------------------

    Result FrameGraph::Compile()
    {
        return m_Scheduler.Compile();
    }

    // -------------------------------------------------------------------------
    // Execute: Dispatch Layers to Task Scheduler
    // -------------------------------------------------------------------------

    void FrameGraph::Execute()
    {
        const auto& layers = m_Scheduler.GetExecutionLayers();

        for (const auto& layer : layers)
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
                    // Capture by value - thunk and pointer are trivially copyable.
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
