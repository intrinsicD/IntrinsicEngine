module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Extrinsic.Runtime.Module;

import Extrinsic.Core.FrameGraph;
import Extrinsic.ECS.Scene.Registry;
export import Extrinsic.Runtime.WorldHandle;
export import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.WorldRegistry;

// ============================================================
// ARCH-011 — RuntimeModule composition seam (ADR-0024 D1/D3/D12/D13).
//
// The app-facing unit of composition. A RuntimeModule binds
// command handlers, event subscriptions, background jobs, the
// Systems it schedules, and its owned state — all through a
// narrow `EngineSetup` capability surface, never an `Engine&`
// (D13). "Kernel" stays a documentation word; the composed class
// is still `Engine` and this is `EngineSetup`.
//
// Two-phase startup (D3): the Engine runs every module's
// `OnRegister` (provide services, register systems/hooks/handlers/
// subscriptions), then every module's `OnResolve` (bind required
// services, fail-closed). Registration order is deliberately never
// load-bearing — inter-module ordering comes from declared System
// Read/Write data dependencies and the two-phase startup.
//
// Shutdown is two-phase by construction (D7): the Engine publishes
// `EngineWillShutDown` and pumps it so every module hears it while
// still alive, then runs `OnShutdown` in reverse registration
// order before substrate teardown.
//
// Layering: kernel substrate per ADR-0024 D9 — no domain nouns in
// the contract; domain state lives in the modules built on it.
// ============================================================

namespace Extrinsic::Runtime
{
    export class EngineSetup;

    // Built-in kernel lifecycle event (ADR-0024 D7). The Engine publishes
    // this and pumps the bus during Shutdown so every module observes it at a
    // pump before its `OnShutdown` runs — the announce/destroy split that
    // makes teardown two-phase.
    export struct EngineWillShutDown
    {
    };

    export struct SystemHandle
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(SystemHandle, SystemHandle) noexcept = default;
    };

    export struct FrameHookHandle
    {
        std::uint64_t Value{0};

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] friend bool operator==(FrameHookHandle, FrameHookHandle) noexcept = default;
    };

    // A per-substep schedulable unit (ADR-0024 D1). `Declare` states the
    // component Read/Write tokens and named signals on the FrameGraph builder;
    // the graph compiles execution order from those declarations, so two
    // modules' systems order by data dependency, never by registration order
    // (D3). `Execute` runs against the active world's registry inside the
    // fixed-step graph. This is the same `Read`/`Write`/`WaitFor`/`Signal`
    // path the built-in `Runtime.EcsSystemBundle` passes use.
    export struct SimSystemDesc
    {
        std::string                                   PassName{};
        std::function<void(Core::FrameGraphBuilder&)> Declare{};
        std::function<void(ECS::Scene::Registry&)>    Execute{};
    };

    // Neutral frame-loop attachment points (ADR-0024 D13). Nothing
    // UI-specific lives in the kernel: `UiBuild` is a slot the future
    // EditorUiModule attaches to; a headless composition attaches nothing and
    // pays nothing.
    export enum class FramePhase : std::uint8_t
    {
        AfterCommandDrain,   // react to fresh command effects, pre-sim
        UiBuild,             // build UI panels (after the variable tick)
        BeforeExtraction,    // last chance to touch sim state this frame
        Maintenance,         // deferred-ops window, post-render
    };

    inline constexpr std::size_t FramePhaseCount = 4;

    // Narrow per-frame capability set handed to frame hooks — no `Engine&`
    // (ADR-0024 D13). Mirrors `CommandContext`'s capability discipline: a hook
    // that needs engine-wide behavior enqueues a command, it does not reach
    // for a god-handle.
    export struct FrameHookContext
    {
        ECS::Scene::Registry& ActiveWorld;
        WorldHandle           ActiveWorldHandle{};
        CommandBus&           Commands;
        EventBus&             Events;
        JobService&           Jobs;
        WorldRegistry&        Worlds;
        double                FrameDeltaSeconds{0.0};
        double                FixedStepAlpha{0.0};
    };

    // The Engine-owned collection point for everything modules register.
    // Deliberately a plain, inspectable data structure (not `Engine`
    // internals): the Engine composes it, the fixed-step loop applies its
    // systems, `RunFrame` invokes its hooks, and contract tests read it back
    // to prove registration-order independence.
    export class ModuleRegistrationSink
    {
    public:
        SystemHandle    AddSimSystem(SimSystemDesc desc);
        FrameHookHandle AddFrameHook(FramePhase phase,
                                     std::function<void(FrameHookContext&)> hook);

        // Append every registered system as a FrameGraph pass bound to the
        // supplied active-world registry. Called once per fixed-step substep
        // after the built-in ECS bundle and before Compile; the same call
        // powers the contract-test schedule inspection.
        void ApplySimSystems(Core::FrameGraph&     graph,
                             ECS::Scene::Registry& scene) const;

        // Invoke every hook registered for `phase`, in registration order.
        // No-ops cheaply when no module registered a hook for the phase.
        void InvokeFrameHooks(FramePhase phase, FrameHookContext& context) const;

        [[nodiscard]] const std::vector<SimSystemDesc>& SimSystems() const noexcept
        {
            return m_SimSystems;
        }
        [[nodiscard]] std::size_t SimSystemCount() const noexcept
        {
            return m_SimSystems.size();
        }
        [[nodiscard]] std::size_t FrameHookCount(FramePhase phase) const noexcept;

        // Drop every registered system and hook (handle counters are not
        // reset). The Engine clears before a re-`Initialize()` so a reused
        // engine re-runs `OnRegister` into an empty sink.
        void Clear() noexcept;

    private:
        struct FrameHookRecord
        {
            FrameHookHandle                        Handle{};
            FramePhase                             Phase{};
            std::function<void(FrameHookContext&)> Hook{};
        };

        std::vector<SimSystemDesc>   m_SimSystems{};
        std::vector<FrameHookRecord> m_FrameHooks{};
        std::uint64_t                m_NextSystemHandle{1};
        std::uint64_t                m_NextHookHandle{1};
    };

    // The narrow registration surface a module receives in `OnRegister`
    // (ADR-0024 D13). The public references expose the kernel substrate the
    // module publishes into; the `Register*` verbs append into the
    // Engine-owned sink. There is no `Engine&`.
    export class EngineSetup
    {
    public:
        EngineSetup(CommandBus&             commands,
                    EventBus&               events,
                    JobService&             jobs,
                    WorldRegistry&          worlds,
                    ServiceRegistry&        services,
                    ModuleRegistrationSink& sink) noexcept
            : Commands(commands)
            , Events(events)
            , Jobs(jobs)
            , Worlds(worlds)
            , Services(services)
            , m_Sink(sink)
        {
        }

        CommandBus&      Commands;
        EventBus&        Events;
        JobService&      Jobs;
        WorldRegistry&   Worlds;
        ServiceRegistry& Services;

        SystemHandle RegisterSimSystem(SimSystemDesc desc)
        {
            return m_Sink.AddSimSystem(std::move(desc));
        }

        FrameHookHandle RegisterFrameHook(FramePhase phase,
                                          std::function<void(FrameHookContext&)> hook)
        {
            return m_Sink.AddFrameHook(phase, std::move(hook));
        }

    private:
        ModuleRegistrationSink& m_Sink;
    };

    // The app-facing unit of composition (ADR-0024 D1/D12). An app is a parts
    // list of these plus initial world content and an initial FrameRecipe.
    // Registration order must not affect behavior (D3).
    export class IRuntimeModule
    {
    public:
        virtual ~IRuntimeModule() = default;

        [[nodiscard]] virtual std::string_view Name() const noexcept = 0;

        // Provide services + register systems/hooks/command-handlers/event
        // subscriptions. Runs for every module before any `OnResolve`.
        virtual void OnRegister(EngineSetup& setup) = 0;

        // Bind required services (`Require` = fail-closed; `Find` = optional).
        // Runs after every module's `OnRegister`, so provide-order never
        // matters.
        virtual void OnResolve(ServiceRegistry& services) { (void)services; }

        // Runs after the Engine has published and pumped `EngineWillShutDown`,
        // in reverse registration order, while all substrate is still alive.
        virtual void OnShutdown() {}
    };
}
