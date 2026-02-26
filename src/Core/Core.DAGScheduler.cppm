module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

export module Core.DAGScheduler;

import Core.Error;
import Core.Logging;

// -------------------------------------------------------------------------
// Core::DAGScheduler - Reusable DAG Scheduling Algorithm
// -------------------------------------------------------------------------
// PURPOSE: Encapsulates the shared directed-acyclic-graph scheduling
// algorithm used by both Core::FrameGraph (CPU system scheduling) and
// Graphics::RenderGraph (GPU render pass orchestration).
//
// Responsibilities:
//   1. Resource state tracking (last writer, current readers per resource)
//   2. Automatic edge insertion from Read/Write hazards (RAW, WAW, WAR)
//   3. Kahn's algorithm topological sort into parallel execution layers
//   4. Direct edge insertion for explicit ordering (labels, manual deps)
//
// The caller owns node data (names, callbacks, barriers, etc.) separately.
// DAGScheduler only manages indices, edges, and layer assignment.
//
// Usage pattern:
//   scheduler.Reset();
//   uint32_t a = scheduler.AddNode();
//   uint32_t b = scheduler.AddNode();
//   scheduler.DeclareWrite(a, resourceKey);
//   scheduler.DeclareRead(b, resourceKey);    // RAW: a → b
//   auto result = scheduler.Compile();
//   const auto& layers = scheduler.GetExecutionLayers();
// -------------------------------------------------------------------------

export namespace Core
{
    class DAGScheduler
    {
    public:
        DAGScheduler();

        // ----- Node Management -----
        // Add a node to the graph. Returns the node index.
        uint32_t AddNode();

        // ----- Hazard-Based Edge Building -----

        // Declare that 'nodeIndex' reads 'resourceKey'.
        // Creates RAW edge from last writer. Registers as reader (participates in future WAR).
        void DeclareRead(uint32_t nodeIndex, size_t resourceKey);

        // Declare that 'nodeIndex' reads 'resourceKey' weakly.
        // Creates RAW edge from last writer but does NOT register as reader.
        // Use for ordering-only dependencies (e.g., label WaitFor) where future
        // writers do not need to wait for this node.
        void DeclareWeakRead(uint32_t nodeIndex, size_t resourceKey);

        // Declare that 'nodeIndex' writes 'resourceKey'.
        // Creates WAW edge from last writer, WAR edges from all current readers.
        // Clears reader list; this node becomes the new exclusive writer.
        void DeclareWrite(uint32_t nodeIndex, size_t resourceKey);

        // ----- Direct Edge Insertion -----
        // Add an explicit ordering edge: producer must complete before consumer.
        // Deduplicates automatically.
        void AddEdge(uint32_t producer, uint32_t consumer);

        // ----- Compile: Topological Sort into Layers -----
        // Performs Kahn's algorithm, grouping nodes into parallel execution layers.
        // Returns Err(InvalidState) on cycle detection.
        [[nodiscard]] Result Compile();

        // ----- Results -----
        [[nodiscard]] uint32_t GetNodeCount() const { return m_ActiveNodeCount; }
        [[nodiscard]] const std::vector<std::vector<uint32_t>>& GetExecutionLayers() const { return m_ExecutionLayers; }
        [[nodiscard]] std::span<const uint32_t> GetDependents(uint32_t nodeIndex) const
        {
            assert(nodeIndex < m_ActiveNodeCount);
            return m_NodePool[nodeIndex].Dependents;
        }
        [[nodiscard]] uint32_t GetIndegree(uint32_t nodeIndex) const
        {
            assert(nodeIndex < m_ActiveNodeCount);
            return m_NodePool[nodeIndex].Indegree;
        }

        // ----- Reset for next frame -----
        void Reset();

    private:
        static constexpr uint32_t kInvalid = ~0u;

        struct NodeData
        {
            std::vector<uint32_t> Dependents; // Outgoing edges
            std::vector<uint32_t> DedupSortedDependents;
            uint32_t Indegree = 0;
        };

        struct ResourceState
        {
            uint32_t LastWriterIndex = kInvalid;
            std::vector<uint32_t> CurrentReaders;
        };

        // Node pool (recycled across frames, grows to high-water mark)
        std::vector<NodeData> m_NodePool;
        uint32_t m_ActiveNodeCount = 0;

        // Resource state tracking (cleared each Reset)
        std::vector<ResourceState> m_ResourceStates;

        // Flat open-addressing hash map used for resource-key -> state-index lookup.
        // We keep this allocator-stable across frames and reset occupancy in O(bucket_count)
        // to avoid per-frame heap churn from std::unordered_map node allocations.
        static constexpr uint32_t kEmptyBucket = ~0u;
        std::vector<uint32_t> m_ResourceLookupBuckets;
        std::vector<size_t> m_ResourceLookupKeys;
        std::vector<uint32_t> m_ResourceLookupValues;

        // Compiled execution layers
        std::vector<std::vector<uint32_t>> m_ExecutionLayers;

        // ----- Helpers -----
        ResourceState& GetResourceState(size_t key);
        void EnsureResourceLookupCapacity(size_t desiredEntryCount);
        void AddEdgeInternal(uint32_t producer, uint32_t consumer);
    };
}
