module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

export module Core:FrameGraph;

import :Error;
import :Memory;
import :Logging;
import :Hash;
import :Tasks;
import :DAGScheduler;

// -------------------------------------------------------------------------
// Core::FrameGraph - System Execution Task Graph
// -------------------------------------------------------------------------
// PURPOSE: Orchestrates per-frame system execution order based on declared
// data dependencies (Read/Write on component types). Mirrors the three-phase
// design of Graphics::RenderGraph (Setup -> Compile -> Execute) but manages
// System scheduling rather than GPU barriers.
//
// Dependency Model:
// Given systems S = {S_1, ..., S_n}, each reading R_i and writing W_i:
//   RAW (Read-after-Write):  S_a writes C, S_b reads C  -> S_a before S_b
//   WAW (Write-after-Write): S_a writes C, S_b writes C -> S_a before S_b
//   WAR (Write-after-Read):  S_a reads C,  S_b writes C -> S_a before S_b
//   RAR (Read-after-Read):   Both read C -> may execute in parallel
//
// Implementation: Delegates DAG scheduling (hazard tracking, Kahn's
// topological sort) to Core::DAGScheduler. FrameGraph owns pass data
// (names, closures) and handles execution dispatch to the task scheduler.
//
// CPU vs GPU:
// The FrameGraph schedules the CPU-side work. For GPU systems (e.g., GPU
// physics), the execute callback records compute dispatches into the
// RenderGraph. The FrameGraph ensures CPU-side preparation ordering is
// correct relative to other game systems.
//
// Memory:
// All per-frame transient data (PassNodes, adjacency lists, closures)
// is allocated in a ScopeStack that is reset each frame. No per-frame
// heap allocation for the hot path.
// -------------------------------------------------------------------------

export namespace Core
{
    class FrameGraph;

    // Compile-time type ID without RTTI.
    // Uses the address of a per-type static as a unique token.
    template <typename T>
    size_t TypeToken()
    {
        static const char s_Anchor{};
        return reinterpret_cast<size_t>(&s_Anchor);
    }

    // -------------------------------------------------------------------------
    // FrameGraphBuilder - Lightweight builder passed to system setup lambdas
    // -------------------------------------------------------------------------
    class FrameGraphBuilder
    {
    public:
        explicit FrameGraphBuilder(FrameGraph& graph, uint32_t passIndex)
            : m_Graph(graph), m_PassIndex(passIndex)
        {
        }

        // Declare Read access to a component type (or global singleton).
        template <typename T>
        void Read();

        // Declare Write access to a component type.
        template <typename T>
        void Write();

        // Declare a generic ordering dependency on a named stage label.
        void WaitFor(Hash::StringID label);

        // Declare that this pass fulfills a named stage label.
        void Signal(Hash::StringID label);

    private:
        FrameGraph& m_Graph;
        uint32_t m_PassIndex;
    };

    // -------------------------------------------------------------------------
    // FrameGraph
    // -------------------------------------------------------------------------
    class FrameGraph
    {
    public:
        explicit FrameGraph(Memory::ScopeStack& scope);

        // ----- 1. Setup Phase -----
        // Register a system pass. SetupFn declares dependencies, ExecuteFn is the work.
        //   SetupFn:   void(FrameGraphBuilder&)
        //   ExecuteFn: void()
        template <typename SetupFn, typename ExecuteFn>
        void AddPass(std::string_view name, SetupFn&& setup, ExecuteFn&& execute)
        {
            uint32_t index = m_Scheduler.AddNode();

            // Grow pass pool if needed
            if (index >= m_PassPool.size())
            {
                m_PassPool.emplace_back();
            }

            auto& pass = m_PassPool[index];
            pass.Name = name;

            // Allocate the closure in the ScopeStack (destructor-safe, no heap alloc).
            struct Closure
            {
                ExecuteFn Func;
            };

            auto closureResult = m_Scope.New<Closure>(std::forward<ExecuteFn>(execute));
            if (!closureResult)
            {
                Log::Error("FrameGraph::AddPass '{}': failed to allocate closure in ScopeStack", name);
                return;
            }

            Closure* closure = *closureResult;

            // Type-erase via function pointer thunk (zero-overhead, no std::function).
            pass.ExecuteFn = +[](void* userData)
            {
                auto* c = static_cast<Closure*>(userData);
                (c->Func)();
            };
            pass.ExecuteUserData = closure;

            // Run the user's setup to declare dependencies.
            FrameGraphBuilder builder(*this, index);
            setup(builder);
        }

        // ----- 2. Compile Phase -----
        // Build the DAG and topologically sort into parallel execution layers.
        // Returns Err on cycle detection (programmer error in dependency declarations).
        [[nodiscard]] Result Compile();

        // ----- 3. Execute Phase -----
        // Dispatch layers to the Task Scheduler. Blocks until all passes complete.
        void Execute();

        // ----- Reset for next frame -----
        void Reset();

        // ----- Introspection -----
        [[nodiscard]] uint32_t GetPassCount() const { return m_Scheduler.GetNodeCount(); }
        [[nodiscard]] const std::vector<std::vector<uint32_t>>& GetExecutionLayers() const { return m_Scheduler.GetExecutionLayers(); }

        // Get the name of a pass by index (for debugging/telemetry).
        [[nodiscard]] std::string_view GetPassName(uint32_t index) const
        {
            assert(index < m_Scheduler.GetNodeCount());
            return m_PassPool[index].Name;
        }

    private:
        friend class FrameGraphBuilder;

        // ----- Internal Types -----
        using ExecuteThunk = void(*)(void*);

        struct PassNode
        {
            std::string_view Name;
            ExecuteThunk ExecuteFn = nullptr;
            void* ExecuteUserData = nullptr;
        };

        // Tag bit to distinguish label resource keys from TypeToken resource keys.
        // TypeTokens are pointer addresses (never have MSB set in user space).
        // Labels use MSB | label.Value to avoid collisions.
        static constexpr size_t kLabelTag = size_t(1) << (sizeof(size_t) * 8 - 1);

        // ----- Data -----
        Memory::ScopeStack& m_Scope;
        DAGScheduler m_Scheduler;

        // Pass pool (recycled across frames, grows to high-water mark)
        std::vector<PassNode> m_PassPool;
    };

    // =========================================================================
    // Template Implementations (must be in interface for module visibility)
    // =========================================================================

    template <typename T>
    void FrameGraphBuilder::Read()
    {
        size_t token = TypeToken<T>();
        m_Graph.m_Scheduler.DeclareRead(m_PassIndex, token);
    }

    template <typename T>
    void FrameGraphBuilder::Write()
    {
        size_t token = TypeToken<T>();
        m_Graph.m_Scheduler.DeclareWrite(m_PassIndex, token);
    }
}
