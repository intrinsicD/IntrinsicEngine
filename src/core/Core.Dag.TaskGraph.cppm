module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

export module Extrinsic.Core.Dag.TaskGraph;

import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.Hash;

// -----------------------------------------------------------------------
// Extrinsic::Core::Dag::TaskGraph — General-purpose closure task graph.
//
// ExecuteCallbacks mode schedules ready closures by dependency and supports
// worker-parallel execution with graph-local completion. PlanOnly mode retains
// the same deterministic topology but requires the caller to consume
// BuildPlan() and explicitly drive any retained callbacks.
//
// API (three phases — must be called in order each epoch/frame):
//
//   1. Setup  — AddPass(name, setup_fn, execute_fn) × N
//   2. Compile — Compile()  → error on dependency cycle
//   3. Consume — Submit() starts callbacks and returns a completion handle,
//                Execute() submits and waits, or BuildPlan() returns metadata
//   4. Reset  — Reset() to begin the next epoch
// -----------------------------------------------------------------------

export namespace Extrinsic::Core::Dag
{
    enum class TaskGraphExecutionMode : std::uint8_t
    {
        ExecuteCallbacks = 0,
        PlanOnly,
    };

    #if __cpp_lib_move_only_function >= 202110L
    using GraphExecuteCallback = std::move_only_function<void()>;
    #else
    using GraphExecuteCallback = std::function<void()>;
    #endif

    class TaskGraph;

    namespace Detail
    {
        template <typename T>
        [[nodiscard]] constexpr std::size_t TypeTokenValue() noexcept
        {
            constexpr auto kMask = std::numeric_limits<std::size_t>::max() >> 1;
#if defined(__clang__) || defined(__GNUC__)
            constexpr std::string_view sig = __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
            constexpr std::string_view sig = __FUNCSIG__;
#else
            constexpr std::string_view sig = "TypeTokenValue<unknown>";
#endif
            constexpr auto ComputeToken = [](std::string_view s) constexpr -> std::size_t {
                uint64_t h = 14695981039346656037ULL;
                for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
                return static_cast<std::size_t>(h) & kMask;
            };
            return ComputeToken(sig);
        }
    }

    // -----------------------------------------------------------------------
    // TaskGraphBuilder — passed to the user's setup lambda in AddPass().
    // Accumulates per-pass resource accesses and ordering constraints.
    // -----------------------------------------------------------------------
    struct TaskGraphPassOptions
    {
        TaskPriority Priority = TaskPriority::Normal;
        uint32_t EstimatedCost = 1;
        bool MainThreadOnly = false;
        bool AllowParallel = true;
        std::string_view DebugCategory = {};
    };

    class TaskGraphBuilder
    {
    public:
        explicit TaskGraphBuilder(TaskGraph& graph, uint32_t passIndex) noexcept
            : m_Graph(graph), m_PassIndex(passIndex) {}

        // --- Data dependency declarations ---

        // TypeToken-based component dependency.
        // Translates the compile-time type token to a ResourceId automatically.
        template <typename T>
        void Read();

        template <typename T>
        void Write();

        // Explicit ResourceId-based resource access.
        // ResourceIds are assigned by the caller and must be consistent within
        // a single epoch (i.e., same ID → same logical resource each frame).
        void ReadResource(ResourceId resource);
        void WriteResource(ResourceId resource);
        void ReadResource(Hash::StringID label);
        void WriteResource(Hash::StringID label);

        // --- Ordering constraints (label-based) ---
        // WaitFor: this pass may not start until all prior passes that
        // Signal(label) in the same graph have completed.
        //
        // If no prior Signal(label) exists, compilation fails with
        // ErrorCode::InvalidState.
        void WaitFor(Hash::StringID label);

        // Signal: this pass "produces" the named label (other passes may WaitFor it).
        void Signal(Hash::StringID label);

        // --- Ordering constraints (explicit) ---
        // Adds a dependency edge from `predecessorPassIndex` to this pass.
        void DependsOn(std::uint32_t predecessorPassIndex);

        // Adds a named dependency edge from `predecessorPassIndex` to this pass.
        // Use when diagnostics should preserve caller-defined edge semantics.
        void DependsOn(std::uint32_t predecessorPassIndex, std::string_view reason);

    private:
        TaskGraph& m_Graph;
        uint32_t   m_PassIndex;
    };

    // -----------------------------------------------------------------------
    // TaskGraphCompletion — shared completion state for one submission.
    //
    // IsReady() is thread-safe. Wait() and PumpMainThreadPasses() must be
    // called on the thread that called TaskGraph::Submit(); that owning thread
    // is responsible for callbacks marked MainThreadOnly or !AllowParallel.
    // -----------------------------------------------------------------------
    class TaskGraphCompletion
    {
    public:
        TaskGraphCompletion() noexcept;
        ~TaskGraphCompletion();
        TaskGraphCompletion(const TaskGraphCompletion&) noexcept;
        TaskGraphCompletion& operator=(const TaskGraphCompletion&) noexcept;
        TaskGraphCompletion(TaskGraphCompletion&&) noexcept;
        TaskGraphCompletion& operator=(TaskGraphCompletion&&) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] Core::Result Wait();
        [[nodiscard]] Core::Expected<std::uint32_t> PumpMainThreadPasses();

    private:
        struct Impl;
        explicit TaskGraphCompletion(std::shared_ptr<Impl> impl) noexcept;

        std::shared_ptr<Impl> m_Impl{};
        friend class TaskGraph;
    };

    // -----------------------------------------------------------------------
    // TaskGraph
    // -----------------------------------------------------------------------
    class TaskGraph
    {
    public:
        explicit TaskGraph(
            TaskGraphExecutionMode mode = TaskGraphExecutionMode::ExecuteCallbacks);
        ~TaskGraph();
        TaskGraph(const TaskGraph&) = delete;
        TaskGraph& operator=(const TaskGraph&) = delete;
        TaskGraph(TaskGraph&&) noexcept;
        TaskGraph& operator=(TaskGraph&&) noexcept;

        // ----- Phase 1: Setup -----

        // Register a pass. setup_fn receives a TaskGraphBuilder to declare
        // dependencies. execute_fn is the work closure (stored as a lightweight
        // move-only wrapper where supported; otherwise falls back to std::function).
        //
        // Returns a PassHandle that can be used to query pass metadata.
        // In PlanOnly mode the execute_fn is stored but never called by
        // Execute(); callers consume BuildPlan() and drive callbacks explicitly.
        template <typename SetupFn, typename ExecuteFn>
        void AddPass(std::string_view name, SetupFn&& setup, ExecuteFn&& execute)
        {
            AddPass(name,
                    TaskGraphPassOptions{},
                    std::forward<SetupFn>(setup),
                    std::forward<ExecuteFn>(execute));
        }

        template <typename SetupFn, typename ExecuteFn>
        void AddPass(std::string_view name,
                     const TaskGraphPassOptions& options,
                     SetupFn&& setup,
                     ExecuteFn&& execute)
        {
            const uint32_t idx = AddPassInternal(name,
                options,
                GraphExecuteCallback(std::forward<ExecuteFn>(execute)));
            TaskGraphBuilder builder(*this, idx);
            setup(builder);
        }

        // Convenience: pass with no resource dependencies (ordering via labels only).
        template <typename ExecuteFn>
        void AddPass(std::string_view name, ExecuteFn&& execute)
        {
            AddPass(name,
                   [](TaskGraphBuilder&){},
                   std::forward<ExecuteFn>(execute));
        }

        template <typename ExecuteFn>
        void AddPass(std::string_view name,
                     const TaskGraphPassOptions& options,
                     ExecuteFn&& execute)
        {
            AddPass(name,
                   options,
                   [](TaskGraphBuilder&){},
                   std::forward<ExecuteFn>(execute));
        }

        // ----- Phase 2: Compile -----

        // Build the topological execution schedule. Must be called once after all
        // AddPass() calls and before Execute()/BuildPlan().
        // Returns Err on cycle detection.
        [[nodiscard]] Core::Result Compile();

        // ----- Phase 3a: Execute callbacks -----

        // Start all dependency-ready callbacks without waiting. The returned
        // handle owns the execution state and may outlive this TaskGraph object.
        // Wait()/PumpMainThreadPasses() are owner-thread operations; IsReady()
        // may be polled from any thread.
        //
        // Returns InvalidState for PlanOnly graphs or a live prior submission.
        [[nodiscard]] Core::Expected<TaskGraphCompletion> Submit();

        // Blocking compatibility path. Equivalent to Submit() followed by
        // completion.Wait(); it help-runs scheduler work and parks when idle.
        //
        // Returns InvalidState when the graph was constructed in PlanOnly mode.
        [[nodiscard]] Core::Result Execute();

        // ----- Phase 3b: Build plan -----

        // Returns the topologically sorted execution plan.
        // The caller iterates the returned vector and drives any external work.
        [[nodiscard]] Core::Expected<std::vector<PlanTask>> BuildPlan();

        // ----- Execute single pass -----
        // Fire the closure registered for pass at `passIndex` (i.e. PlanTask::id.Index).
        // Asserts that passIndex is in range.
        void ExecutePass(uint32_t passIndex);

        // Move the execute closure out of the pass node so it can be dispatched
        // to a worker thread that outlives the next Reset() call.
        // After this call the pass's stored closure is null (no double-fire).
        [[nodiscard]] GraphExecuteCallback TakePassExecute(uint32_t passIndex);

        // ----- Reset -----
        // Fails closed while a submitted execution is still live.
        [[nodiscard]] Core::Result Reset();

        // ----- Introspection -----
        [[nodiscard]] uint32_t PassCount() const noexcept;
        [[nodiscard]] const std::vector<std::vector<uint32_t>>& GetExecutionLayers() const noexcept;
        [[nodiscard]] std::string_view PassName(uint32_t index) const noexcept;
        [[nodiscard]] uint64_t LastCompileTimeNs()     const noexcept;
        [[nodiscard]] uint64_t LastExecuteTimeNs()     const noexcept;
        [[nodiscard]] uint64_t LastCriticalPathTimeNs()const noexcept;
        [[nodiscard]] ScheduleStats GetScheduleStats() const noexcept;

    private:
        friend class TaskGraphBuilder;

        // Called by the typed AddPass template above.
        uint32_t AddPassInternal(std::string_view name,
                                 const TaskGraphPassOptions& options,
                                 GraphExecuteCallback execute);

        // TypeToken → ResourceId translation (stable within an epoch; reset on Reset()).
        ResourceId TokenToResource(std::size_t token);
        ResourceId StringIdToResource(Hash::StringID stringId);
        void NormalizeOptions(TaskGraphPassOptions& options) const;

        struct Impl;
        std::shared_ptr<Impl> m_Impl;
    };

    // -----------------------------------------------------------------------
    // Template implementations (must be in interface for module visibility)
    // -----------------------------------------------------------------------
    template <typename T>
    void TaskGraphBuilder::Read()
    {
        // Compute a compile-time type token then delegate to the explicit-id path.
        ReadResource(m_Graph.TokenToResource(Detail::TypeTokenValue<T>()));
    }

    template <typename T>
    void TaskGraphBuilder::Write()
    {
        WriteResource(m_Graph.TokenToResource(Detail::TypeTokenValue<T>()));
    }

    // -----------------------------------------------------------------------
    // Factory (produces a concrete TaskGraph implementation)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::unique_ptr<TaskGraph> CreateTaskGraph(
        TaskGraphExecutionMode mode = TaskGraphExecutionMode::ExecuteCallbacks);
}
