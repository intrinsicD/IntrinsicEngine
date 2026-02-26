module;

#include <cassert>
#include <atomic>
#include <cstdint>
#include <memory>
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
        const uint32_t nodeCount = m_Scheduler.GetNodeCount();
        if (nodeCount == 0)
            return;

        // Fast path: single pass or no scheduler — run inline in layer order.
        if (nodeCount == 1 || !Tasks::Scheduler::IsInitialized())
        {
            for (const auto& layer : m_Scheduler.GetExecutionLayers())
            {
                for (uint32_t nodeIdx : layer)
                {
                    auto& pass = m_PassPool[nodeIdx];
                    pass.ExecuteFn(pass.ExecuteUserData);
                }
            }
            return;
        }

        // Execute dependency-ready passes continuously. Layer data remains available
        // via DAGScheduler introspection for diagnostics/visualization only.
        struct FrameGraphExecutionState
        {
            std::unique_ptr<std::atomic<uint32_t>[]> RemainingDependencies;
            uint32_t NodeCount = 0;
        };

        FrameGraphExecutionState state{};
        state.NodeCount = nodeCount;
        state.RemainingDependencies = std::make_unique<std::atomic<uint32_t>[]>(nodeCount);

        for (uint32_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx)
        {
            state.RemainingDependencies[nodeIdx].store(m_Scheduler.GetIndegree(nodeIdx), std::memory_order_relaxed);
        }

        auto executePassAndReleaseDependents = [&](auto&& self, uint32_t nodeIdx) -> void
        {
            auto& pass = m_PassPool[nodeIdx];
            pass.ExecuteFn(pass.ExecuteUserData);

            for (uint32_t dependent : m_Scheduler.GetDependents(nodeIdx))
            {
                const uint32_t prior = state.RemainingDependencies[dependent].fetch_sub(1, std::memory_order_acq_rel);
                assert(prior > 0 && "FrameGraph dependent indegree underflow");

                if (prior == 1)
                {
                    Tasks::Scheduler::Dispatch([&self, dependent]() {
                        self(self, dependent);
                    });
                }
            }
        };

        uint32_t readyCount = 0;
        for (uint32_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx)
        {
            if (state.RemainingDependencies[nodeIdx].load(std::memory_order_relaxed) == 0)
            {
                ++readyCount;
                Tasks::Scheduler::Dispatch([&executePassAndReleaseDependents, nodeIdx]() {
                    executePassAndReleaseDependents(executePassAndReleaseDependents, nodeIdx);
                });
            }
        }

        assert(readyCount > 0 && "FrameGraph::Execute expected at least one ready root node after successful compile");
        Tasks::Scheduler::WaitForAll();
    }
}
