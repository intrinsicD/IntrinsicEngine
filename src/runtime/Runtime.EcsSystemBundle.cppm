module;

#include <cstdint>

export module Extrinsic.Runtime.EcsSystemBundle;

import Extrinsic.Core.FrameGraph;
import Extrinsic.ECS.Scene.Registry;

// ============================================================
// RUNTIME-091 — Promoted ECS system bundle activation.
//
// Runtime-owned helper that registers the baseline ECS systems
// (currently `Extrinsic.ECS.System.TransformHierarchy` and
// `Extrinsic.ECS.System.BoundsPropagation`) into the fixed-step
// `Core::FrameGraph` each substep. Runtime composition calls this
// helper after `IApplication::OnSimTick` has had a chance to
// register app passes and before `Core::FrameGraph::Compile`, so
// dependency edges declared by the systems (`Read`/`Write` on
// component TypeTokens, and the named `TransformUpdate` /
// `WorldBoundsUpdate` signals) resolve naturally against any app
// passes that mutate transforms or wait on the propagation seam.
//
// The helper is intentionally split out of `Runtime.Engine` so it
// can be exercised in contract tests without driving a full
// `Engine::Run()` loop, and so future runtime bundle composition
// extensions stay in a single, focused module.
//
// Layering: runtime depends on `ecs` and `core`; ECS never imports
// runtime, so the activation glue lives here and not in
// `src/ecs/Systems`.
// ============================================================

export namespace Extrinsic::Runtime
{
    // Per-call summary of which baseline systems the helper actually
    // registered. The counters never decrement and are intended for
    // diagnostics + tests; nothing in the production engine path
    // reads them. `Registered` is the total number of FrameGraph
    // passes appended by this helper invocation.
    struct PromotedEcsSystemBundleStats
    {
        std::uint32_t Registered{0};
        bool TransformHierarchyRegistered{false};
        bool BoundsPropagationRegistered{false};
    };

    // Register the baseline runtime-owned ECS system passes into the
    // supplied FrameGraph for the supplied scene registry. Safe to
    // call once per fixed-step substep; the FrameGraph is reset
    // between substeps so passes do not accumulate across ticks.
    //
    // Passes registered (in declaration order; FrameGraph compile
    // resolves the actual execution order via TypeToken and named
    // signals):
    //   * `TransformHierarchy::PassName` — recomputes dirty world
    //     matrices, stamps `WorldUpdatedTag`, clears `IsDirtyTag`.
    //   * `BoundsPropagation::PassName` — `WaitFor("TransformUpdate")`
    //     and recomputes world bounds for entities tagged this frame.
    //
    // `RenderSync` is intentionally NOT registered here: per the ECS
    // systems README, GPU-handle-touching residency belongs to the
    // runtime render-extraction lane, not to a CPU FrameGraph pass.
    [[nodiscard]] PromotedEcsSystemBundleStats RegisterPromotedEcsSystemBundle(
        Core::FrameGraph& graph,
        ECS::Scene::Registry& scene);
}
