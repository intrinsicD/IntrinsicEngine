module;

#include <cassert>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include <chrono>
#include <algorithm>

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
        m_LastCompileTimeNs = 0;
        m_LastExecuteTimeNs = 0;
        m_LastCriticalPathTimeNs = 0;
        m_LastRootReadyCount = 0;
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
        const auto compileStart = std::chrono::steady_clock::now();
        auto result = m_Scheduler.Compile();
        m_LastCompileTimeNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - compileStart).count());
        return result;
    }

    // -------------------------------------------------------------------------
    // Execute: Dispatch Layers to Task Scheduler
    // -------------------------------------------------------------------------

    void FrameGraph::Execute()
    {
        const uint32_t nodeCount = m_Scheduler.GetNodeCount();
        if (nodeCount == 0)
            return;

        const auto executeStart = std::chrono::steady_clock::now();
        std::vector<uint64_t> nodeDurationsNs(nodeCount, 0);

        // Fast path: single pass or no scheduler — run inline in layer order.
        if (nodeCount == 1 || !Tasks::Scheduler::IsInitialized())
        {
            for (const auto& layer : m_Scheduler.GetExecutionLayers())
            {
                for (uint32_t nodeIdx : layer)
                {
                    auto& pass = m_PassPool[nodeIdx];
                    const auto passStart = std::chrono::steady_clock::now();
                    pass.ExecuteFn(pass.ExecuteUserData);
                    nodeDurationsNs[nodeIdx] = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - passStart).count());
                }
            }
        }
        else
        {
            // Execute dependency-ready passes continuously. Layer data remains available
            // via DAGScheduler introspection for diagnostics/visualization only.
            struct FrameGraphExecutionState
            {
                std::unique_ptr<std::atomic<uint32_t>[]> RemainingDependencies;
                // CAS-guarded dispatch flags: each node may only be dispatched once.
                // Prevents double-execution under high contention regardless of
                // indegree bookkeeping races in the task scheduler.
                std::unique_ptr<std::atomic<uint32_t>[]> DispatchedFlags;
                uint32_t NodeCount = 0;
            };

            struct ExecutionContext
            {
                FrameGraph& Graph;
                FrameGraphExecutionState& State;
                std::vector<uint64_t>& NodeDurationsNs;
            };

            FrameGraphExecutionState state{};
            state.NodeCount = nodeCount;
            state.RemainingDependencies = std::make_unique<std::atomic<uint32_t>[]>(nodeCount);
            state.DispatchedFlags = std::make_unique<std::atomic<uint32_t>[]>(nodeCount);

            for (uint32_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx)
            {
                state.RemainingDependencies[nodeIdx].store(m_Scheduler.GetIndegree(nodeIdx), std::memory_order_relaxed);
                state.DispatchedFlags[nodeIdx].store(0, std::memory_order_relaxed);
            }

            // Full fence: guarantee all stores above are globally visible
            // before any root task is dispatched to worker threads.
            std::atomic_thread_fence(std::memory_order_seq_cst);

            const auto executePassAndReleaseDependents = [](ExecutionContext ctx, uint32_t nodeIdx, auto dispatchSelf) -> void
            {
                auto& pass = ctx.Graph.m_PassPool[nodeIdx];
                const auto passStart = std::chrono::steady_clock::now();
                pass.ExecuteFn(pass.ExecuteUserData);
                ctx.NodeDurationsNs[nodeIdx] = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - passStart).count());

                for (uint32_t dependent : ctx.Graph.m_Scheduler.GetDependents(nodeIdx))
                {
                    const uint32_t prior = ctx.State.RemainingDependencies[dependent].fetch_sub(1, std::memory_order_acq_rel);
                    assert(prior > 0 && "FrameGraph dependent indegree underflow");

                    if (prior == 1)
                    {
                        uint32_t expected = 0;
                        if (ctx.State.DispatchedFlags[dependent].compare_exchange_strong(
                                expected, 1, std::memory_order_acq_rel, std::memory_order_relaxed))
                        {
                            Tasks::Scheduler::Dispatch([ctx, dependent, dispatchSelf]() {
                                dispatchSelf(ctx, dependent, dispatchSelf);
                            });
                        }
                    }
                }
            };

            const ExecutionContext execCtx{*this, state, nodeDurationsNs};
            uint32_t readyCount = 0;
            for (uint32_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx)
            {
                if (state.RemainingDependencies[nodeIdx].load(std::memory_order_acquire) == 0)
                {
                    ++readyCount;
                    uint32_t expected = 0;
                    if (state.DispatchedFlags[nodeIdx].compare_exchange_strong(
                            expected, 1, std::memory_order_acq_rel, std::memory_order_relaxed))
                    {
                        Tasks::Scheduler::Dispatch([execCtx, nodeIdx, executePassAndReleaseDependents]() {
                            executePassAndReleaseDependents(execCtx, nodeIdx, executePassAndReleaseDependents);
                        });
                    }
                }
            }

            m_LastRootReadyCount = readyCount;
            assert(readyCount > 0 && "FrameGraph::Execute expected at least one ready root node after successful compile");
            Tasks::Scheduler::WaitForAll();
        }

        m_LastExecuteTimeNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - executeStart).count());

        std::vector<uint64_t> longestPathNs(nodeCount, 0);
        for (uint32_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx)
        {
            if (m_Scheduler.GetIndegree(nodeIdx) == 0)
                longestPathNs[nodeIdx] = nodeDurationsNs[nodeIdx];
        }

        for (const auto& layer : m_Scheduler.GetExecutionLayers())
        {
            for (uint32_t nodeIdx : layer)
            {
                const uint64_t prefix = longestPathNs[nodeIdx];
                for (uint32_t dependent : m_Scheduler.GetDependents(nodeIdx))
                {
                    const uint64_t candidate = prefix + nodeDurationsNs[dependent];
                    longestPathNs[dependent] = std::max(longestPathNs[dependent], candidate);
                }
            }
        }

        uint64_t criticalPathNs = 0;
        for (uint64_t value : longestPathNs)
            criticalPathNs = std::max(criticalPathNs, value);
        m_LastCriticalPathTimeNs = criticalPathNs;
    }

}
