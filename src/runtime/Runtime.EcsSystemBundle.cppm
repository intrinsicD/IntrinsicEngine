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
        bool RenderSyncRegistered{false};
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
    //   * `RenderSync::PassName` — `WaitFor("TransformUpdate")` and
    //     `WaitFor("WorldBoundsUpdate")`, forwards `WorldUpdatedTag`
    //     into `DirtyTags::DirtyTransform` for the runtime render
    //     extraction lane to drain, then clears `WorldUpdatedTag`.
    //
    // `RenderSync` is a CPU-only tag-forwarding pass per the
    // `HARDEN-066` policy decision; GPU-handle-touching residency
    // continues to live in the runtime render-extraction lane, not
    // in any ECS system.
    [[nodiscard]] PromotedEcsSystemBundleStats RegisterPromotedEcsSystemBundle(
        Core::FrameGraph& graph,
        ECS::Scene::Registry& scene);

    // BUG-024 — Observables for the runtime-owned pre-render transform
    // flush. `WorldUpdatedObserved` counts entities whose world matrix was
    // rewritten by this flush invocation; `DirtyTransformStamped` counts
    // entities forwarded into `DirtyTags::DirtyTransform` for the render
    // extraction lane to drain this frame.
    struct PreRenderTransformFlushStats
    {
        std::uint32_t WorldUpdatedObserved{0};
        std::uint32_t DirtyTransformStamped{0};
        std::uint32_t WorldUpdatedCleared{0};
    };

    // BUG-024 — Pre-render transform flush. Runs the promoted baseline ECS
    // systems (`TransformHierarchy` → `BoundsPropagation` → `RenderSync`)
    // directly, outside the fixed-step FrameGraph, so local-transform
    // mutations made after the scheduled fixed-step bundle — Sandbox Editor
    // UI inspector edits (via the ImGui editor hook), `OnVariableTick()`
    // app mutations, and `GizmoInteraction` drags — are reflected in
    // `Transform::WorldMatrix`, world bounds, and
    // `DirtyTags::DirtyTransform` before render extraction observes the
    // scene. Entities with no pending `Transform::IsDirtyTag` are untouched;
    // a clean scene makes this a cheap no-op traversal.
    //
    // Runtime composition calls this from `Engine::RunFrame()` after the
    // last pre-render mutation source and before transform-gizmo packet
    // build + `RenderExtractionCache::ExtractAndSubmit()`.
    PreRenderTransformFlushStats FlushPreRenderTransformState(
        ECS::Scene::Registry& scene);
}
