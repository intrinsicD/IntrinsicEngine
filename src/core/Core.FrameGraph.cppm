module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

export module Extrinsic.Core.FrameGraph;

import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.Hash;
import Extrinsic.Core.Memory;

// -----------------------------------------------------------------------
// Extrinsic::Core::FrameGraph — ECS system execution graph (CPU domain).
//
// Wraps a Dag::TaskGraph (QueueDomain::Cpu) and layers TypeToken-based
// ECS component dependency declarations on top of it.
//
// Dependency model (per frame):
//   Given systems S_i with declared reads R_i and writes W_i:
//     RAW: S_a writes C, S_b reads  C → S_a before S_b
//     WAW: S_a writes C, S_b writes C → S_a before S_b
//     WAR: S_a reads  C, S_b writes C → S_a before S_b
//     RAR: both read   C → may execute in parallel (same layer)
//
// TypeToken<T>() is a compile-time FNV-1a hash of the type's compiler
// signature — stable across TUs and named-module boundaries (no RTTI).
//
// Usage:
//   FrameGraph fg;
//   fg.AddPass("TransformUpdate",
//       [](FrameGraphBuilder& b){ b.Write<Transform::Component>(); },
//       []{ /* work */ });
//   fg.Compile();
//   fg.Execute();   // fires passes in topo-layer order
//   fg.Reset();     // reset for next frame
// -----------------------------------------------------------------------

export namespace Extrinsic::Core
{
    class FrameGraph;

    // -----------------------------------------------------------------------
    // Compile-time type ID — deterministic FNV-1a hash of compiler type sig.
    // Stable across TUs and named-module boundaries (no RTTI).
    // -----------------------------------------------------------------------
    namespace Detail
    {
        [[nodiscard]] constexpr uint64_t HashTypeSig(std::string_view s) noexcept
        {
            uint64_t h = 14695981039346656037ULL;
            for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
            return h;
        }

        template <typename T>
        [[nodiscard]] constexpr std::string_view TypeSig() noexcept
        {
#if defined(__clang__) || defined(__GNUC__)
            return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
            return __FUNCSIG__;
#else
            return "TypeSig<unknown>";
#endif
        }
    }

    template <typename T>
    [[nodiscard]] std::size_t TypeToken() noexcept
    {
        constexpr auto kMask = std::numeric_limits<std::size_t>::max() >> 1;
        static constexpr std::size_t s_Token = static_cast<std::size_t>(
            Detail::HashTypeSig(Detail::TypeSig<T>())) & kMask;
        return s_Token;
    }

    // -----------------------------------------------------------------------
    // FrameGraph pass options — scheduling metadata for the TaskGraph backend.
    // -----------------------------------------------------------------------
    struct FrameGraphPassOptions
    {
        Dag::TaskPriority Priority = Dag::TaskPriority::Normal;
        uint32_t EstimatedCost = 1;
        bool MainThreadOnly = false;
        bool AllowParallel = true;
        std::string_view DebugCategory = {};
    };

    [[nodiscard]] constexpr Dag::TaskGraphPassOptions ToTaskGraphPassOptions(const FrameGraphPassOptions& options) noexcept
    {
        return {
            .Priority = options.Priority,
            .EstimatedCost = options.EstimatedCost,
            .MainThreadOnly = options.MainThreadOnly,
            .AllowParallel = options.AllowParallel,
            .DebugCategory = options.DebugCategory,
        };
    }

    // -----------------------------------------------------------------------
    // Built-in structural and phase tokens.
    // -----------------------------------------------------------------------
    inline constexpr Hash::StringID RegistryStructureToken = Hash::StringID{"FrameGraph.RegistryStructure"};
    inline constexpr Hash::StringID SceneCommitToken = Hash::StringID{"FrameGraph.SceneCommit"};
    inline constexpr Hash::StringID RenderExtractionToken = Hash::StringID{"FrameGraph.RenderExtraction"};

    // -----------------------------------------------------------------------
    // FrameGraphBuilder — passed to user setup lambdas in AddPass().
    // Thin adapter: translates TypeToken → TaskGraph::Read/WriteResource.
    // -----------------------------------------------------------------------
    class FrameGraphBuilder
    {
    public:
        explicit FrameGraphBuilder(Dag::TaskGraphBuilder inner) noexcept
            : m_Inner(std::move(inner)) {}

        // Declare read dependency on a component / singleton type.
        template <typename T>
        void Read()  { m_Inner.Read<T>(); }

        // Declare write dependency on a component / singleton type.
        template <typename T>
        void Write() { m_Inner.Write<T>(); }

        // Named ordering constraints (stage labels).
        void WaitFor(Hash::StringID label) { m_Inner.WaitFor(label); }
        void Signal (Hash::StringID label) { m_Inner.Signal(label);  }

        // Structural safety declarations.
        void StructuralRead() { m_Inner.ReadResource(RegistryStructureToken); }
        void StructuralWrite() { m_Inner.WriteResource(RegistryStructureToken); }

        // Frame-pipeline phase barriers.
        void CommitWorld() { m_Inner.WriteResource(SceneCommitToken); }
        void CommitTick() { CommitWorld(); }

        // Resource declarations at framegraph level.
        void ReadResource(Dag::ResourceId resource) { m_Inner.ReadResource(resource); }
        void WriteResource(Dag::ResourceId resource) { m_Inner.WriteResource(resource); }
        void ReadResource(Hash::StringID label) { m_Inner.ReadResource(label); }
        void WriteResource(Hash::StringID label) { m_Inner.WriteResource(label); }

    private:
        Dag::TaskGraphBuilder m_Inner;
    };

    // -----------------------------------------------------------------------
    // FrameGraph
    // -----------------------------------------------------------------------
    class FrameGraph
    {
    public:
        FrameGraph();
        ~FrameGraph();
        FrameGraph(const FrameGraph&) = delete;
        FrameGraph& operator=(const FrameGraph&) = delete;
        FrameGraph(FrameGraph&&) noexcept;
        FrameGraph& operator=(FrameGraph&&) noexcept;

        // ----- Phase 1: Setup -----

        // Register a system pass.
        //   SetupFn:   void(FrameGraphBuilder&)
        //   ExecuteFn: void()
        template <typename SetupFn, typename ExecuteFn>
        void AddPass(std::string_view name, SetupFn&& setup, ExecuteFn&& execute)
        {
            AddPass(name,
                    FrameGraphPassOptions{},
                    std::forward<SetupFn>(setup),
                    std::forward<ExecuteFn>(execute));
        }

        template <typename SetupFn, typename ExecuteFn>
        void AddPass(std::string_view name,
                     const FrameGraphPassOptions& options,
                     SetupFn&& setup,
                     ExecuteFn&& execute)
        {
            m_Graph->AddPass(name,
                ToTaskGraphPassOptions(options),
                [setupFn = std::forward<SetupFn>(setup)](Dag::TaskGraphBuilder& b) mutable
                {
                    FrameGraphBuilder fgb(std::move(b));
                    setupFn(fgb);
                    // b is moved-from; forward back is intentional void.
                    (void)b;
                },
                std::forward<ExecuteFn>(execute));
        }

        template <typename ExecuteFn>
        void AddPass(std::string_view name, ExecuteFn&& execute)
        {
            AddPass(name,
                    FrameGraphPassOptions{},
                    [](FrameGraphBuilder&) {},
                    std::forward<ExecuteFn>(execute));
        }

        template <typename ExecuteFn>
        void AddPass(std::string_view name,
                     const FrameGraphPassOptions& options,
                     ExecuteFn&& execute)
        {
            AddPass(name,
                   options,
                   [](FrameGraphBuilder&) {},
                   std::forward<ExecuteFn>(execute));
        }

        // ----- Phase 2: Compile -----
        [[nodiscard]] Core::Result Compile();

        // ----- Phase 3: Execute -----
        [[nodiscard]] Core::Result Execute();
        [[nodiscard]] Core::Expected<std::vector<Dag::PlanTask>> BuildPlan(
            const Dag::BuildConfig& config = {});

        // ----- Reset -----
        void Reset();

        // ----- Introspection -----
        [[nodiscard]] uint32_t PassCount()            const noexcept;
        [[nodiscard]] std::string_view PassName(uint32_t i) const noexcept;
        [[nodiscard]] const std::vector<std::vector<uint32_t>>& GetExecutionLayers() const noexcept;
        [[nodiscard]] Dag::ScheduleStats GetScheduleStats() const noexcept;
        [[nodiscard]] uint64_t LastCompileTimeNs()    const noexcept;
        [[nodiscard]] uint64_t LastExecuteTimeNs()    const noexcept;
        [[nodiscard]] uint64_t LastCriticalPathTimeNs() const noexcept;

    private:
        std::unique_ptr<Dag::TaskGraph> m_Graph;
    };
}
