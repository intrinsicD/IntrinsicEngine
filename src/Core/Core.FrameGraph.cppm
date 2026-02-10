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

// -------------------------------------------------------------------------
// Core::FrameGraph - System Execution Task Graph
// -------------------------------------------------------------------------
// PURPOSE: Orchestrates per-frame system execution order based on declared
// data dependencies (Read/Write on component types). Mirrors the three-phase
// design of Graphics::RenderGraph (Setup → Compile → Execute) but manages
// System scheduling rather than GPU barriers.
//
// Dependency Model:
// Given systems S = {S_1, ..., S_n}, each reading R_i and writing W_i:
//   RAW (Read-after-Write):  S_a writes C, S_b reads C  → S_a before S_b
//   WAW (Write-after-Write): S_a writes C, S_b writes C → S_a before S_b
//   WAR (Write-after-Read):  S_a reads C,  S_b writes C → S_a before S_b
//   RAR (Read-after-Read):   Both read C → may execute in parallel
//
// Compile() builds the DAG via Kahn's algorithm into parallel execution
// layers. Execute() dispatches each layer to the Core::Tasks::Scheduler
// and waits for each layer to complete before starting the next.
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
            uint32_t index = m_ActivePassCount;

            // Grow pass pool if needed
            if (index >= m_PassPool.size())
            {
                m_PassPool.emplace_back();
            }

            auto& pass = m_PassPool[index];
            pass.Name = name;
            pass.Dependents.clear();
            pass.Indegree = 0;

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

            ++m_ActivePassCount;

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
        [[nodiscard]] uint32_t GetPassCount() const { return m_ActivePassCount; }
        [[nodiscard]] const std::vector<std::vector<uint32_t>>& GetExecutionLayers() const { return m_ExecutionLayers; }

        // Get the name of a pass by index (for debugging/telemetry).
        [[nodiscard]] std::string_view GetPassName(uint32_t index) const
        {
            assert(index < m_ActivePassCount);
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

            // DAG edges (outgoing) and indegree
            std::vector<uint32_t> Dependents;
            uint32_t Indegree = 0;
        };

        struct ResourceState
        {
            static constexpr uint32_t kInvalid = ~0u;
            uint32_t LastWriterIndex = kInvalid;
            std::vector<uint32_t> CurrentReaders;
        };

        // ----- Data -----
        Memory::ScopeStack& m_Scope;

        // Pass pool (recycled across frames, grows to high-water mark)
        std::vector<PassNode> m_PassPool;
        uint32_t m_ActivePassCount = 0;

        // Compiled execution layers
        std::vector<std::vector<uint32_t>> m_ExecutionLayers;

        // Dependency tracking (cleared each Compile)
        // Key: TypeToken<T> hash for component types
        std::vector<std::pair<size_t, ResourceState>> m_ResourceStates;
        // Key: StringID value for named labels
        std::vector<std::pair<uint32_t, ResourceState>> m_LabelStates;

        // ----- Helpers -----
        void AddDependency(uint32_t producer, uint32_t consumer);
        ResourceState& GetResourceState(size_t typeToken);
        ResourceState& GetLabelState(Hash::StringID label);
    };

    // =========================================================================
    // Template Implementations (must be in interface for module visibility)
    // =========================================================================

    template <typename T>
    void FrameGraphBuilder::Read()
    {
        size_t token = TypeToken<T>();
        auto& state = m_Graph.GetResourceState(token);

        // RAW: If someone wrote this resource, depend on the last writer.
        if (state.LastWriterIndex != FrameGraph::ResourceState::kInvalid)
        {
            m_Graph.AddDependency(state.LastWriterIndex, m_PassIndex);
        }

        state.CurrentReaders.push_back(m_PassIndex);
    }

    template <typename T>
    void FrameGraphBuilder::Write()
    {
        size_t token = TypeToken<T>();
        auto& state = m_Graph.GetResourceState(token);

        // WAW: Depend on last writer.
        if (state.LastWriterIndex != FrameGraph::ResourceState::kInvalid)
        {
            m_Graph.AddDependency(state.LastWriterIndex, m_PassIndex);
        }

        // WAR: Depend on all current readers (they must finish before we overwrite).
        for (uint32_t readerIdx : state.CurrentReaders)
        {
            if (readerIdx != m_PassIndex)
            {
                m_Graph.AddDependency(readerIdx, m_PassIndex);
            }
        }

        // We become the exclusive owner.
        state.CurrentReaders.clear();
        state.LastWriterIndex = m_PassIndex;
    }
}
