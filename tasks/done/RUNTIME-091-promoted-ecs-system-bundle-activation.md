# RUNTIME-091 — Activate promoted ECS system bundle in fixed-step runtime

## Status

- Status: done.
- Owner/agent: Claude on `claude/setup-agentic-workflow-nSRVg`.
- Branch: `claude/setup-agentic-workflow-nSRVg`.
- Started: 2026-05-15.
- Completed: 2026-05-15.
- Implementation commit: `7c088f6` on branch `claude/setup-agentic-workflow-nSRVg`.
- Landed slice: introduced `Extrinsic.Runtime.EcsSystemBundle::RegisterPromotedEcsSystemBundle(FrameGraph&, ECS::Scene::Registry&)`, called from `Engine::RunFrame` between `IApplication::OnSimTick` and `Core::FrameGraph::Compile` so `TransformHierarchy` + `BoundsPropagation` run every fixed-step substep. Added contract coverage in `tests/contract/runtime/Test.RuntimeEcsSystemBundle.cpp` (bundle helper) and a layering scan in `tests/contract/runtime/Test.RuntimeEngineLayering.cpp` (engine wiring), refreshed `docs/api/generated/module_inventory.md`, and reconciled `src/ecs/Systems/README.md` + `src/runtime/README.md` + `docs/migration/nonlegacy-parity-matrix.md` with the now-active activation path.

## Goal
- Register or invoke promoted ECS systems from runtime fixed-step composition so `Extrinsic.ECS.System.TransformHierarchy` runs deterministically before render extraction without requiring app-specific manual wiring.

## Non-goals
- No new ECS component semantics.
- No graphics/RHI residency changes.
- No scene serialization, editor UI, or physics integration.
- No migration or deletion of legacy ECS modules.

## Context
- Owner/layer: `runtime`; runtime owns composition and may depend on `ecs`, `core`, `graphics`, and lower layers.
- Source review: [`docs/reviews/2026-05-13-src-ecs-gap-analysis.md`](../../docs/reviews/2026-05-13-src-ecs-gap-analysis.md).
- `HARDEN-061` promoted `Extrinsic.ECS.System.TransformHierarchy::{OnUpdate,RegisterSystem}` but intentionally deferred runtime activation.
- `Runtime.Engine` already drives fixed-step `OnSimTick` and a CPU `Core::FrameGraph`; this task decides the runtime-owned bundle/wiring path for baseline ECS systems.
- ECS must not import runtime; activation belongs here, not in `src/ecs`.

## Required changes
- [x] Define the promoted runtime ECS system bundle surface or explicit invocation path for fixed-step systems. (`src/runtime/Runtime.EcsSystemBundle.cppm` + `.cpp` export `RegisterPromotedEcsSystemBundle(FrameGraph&, ECS::Scene::Registry&)` with a `PromotedEcsSystemBundleStats` summary; `src/runtime/CMakeLists.txt` adds the module to `ExtrinsicRuntime`.)
- [x] Ensure `TransformHierarchy` runs after gameplay/app fixed-step mutations and before `Runtime.RenderExtractionCache::ExtractAndSubmit` observes world matrices. (`src/runtime/Runtime.Engine.cpp:613` invokes the bundle after `m_Application->OnSimTick` and before `m_FrameGraph->Compile`, inside the substep loop that runs before `Core::ExecuteRenderFrameContract`.)
- [x] Preserve app extensibility: applications must still be able to add their own fixed-step passes without bypassing the baseline ECS update order. (App passes registered through `engine.GetFrameGraph().AddPass(...)` resolve through TypeToken reads/writes and the named `TransformUpdate` / `WorldBoundsUpdate` signals; `tests/contract/runtime/Test.RuntimeEcsSystemBundle.cpp::AppPassMutatingTransformRunsBeforeTransformHierarchy` exercises this ordering.)
- [x] Add diagnostics or assertions for failed CPU frame-graph registration/compilation if the selected path uses `RegisterSystem`. (Engine continues to log `[Runtime] FrameGraph Compile() failed` / `Execute() failed` via `Core::Log::Error` and resets the graph; the bundle returns a `PromotedEcsSystemBundleStats` summary that downstream callers can inspect for which passes registered this substep.)
- [x] Record any intentionally unregistered ECS systems as non-goals or follow-up tasks. (`Runtime.EcsSystemBundle.cppm` documents that `Extrinsic.ECS.System.RenderSync` is intentionally excluded; GPU-handle-touching residency stays in `Runtime.RenderExtraction`. Future ECS systems can extend the bundle as they promote.)

## Tests
- [x] Add or update `tests/contract/runtime/Test.RuntimeEcsSystemBundle.cpp` (equivalent runtime integration coverage proving dirty transforms are propagated during a headless frame before extraction).
- [x] Cover at least one hierarchy child case where a parent dirty transform updates the child world matrix through runtime-owned scheduling. (`BundleExecutionPropagatesDirtyChildWorldMatrix` covers parent→child world matrix composition + `WorldUpdatedTag` stamping + `IsDirtyTag` clearing.)
- [x] Keep tests CPU/headless and label them `integration;runtime` or `integration;runtime;ecs` using existing label policy. (Bundled into `IntrinsicRuntimeContractTests`, labels `contract runtime`; the new layering check `RunFrameRegistersPromotedEcsSystemBundleBetweenSimTickAndCompile` rides in `IntrinsicRuntimeIntegrationTests` with labels `integration runtime`.)

## Docs
- [x] Update `src/ecs/Systems/README.md` to replace the deferred-activation note with the factual runtime-owned activation path.
- [x] Update `src/runtime/` or `docs/architecture/runtime.md` documentation if a new runtime bundle API is introduced. (`src/runtime/README.md` adds the `Extrinsic.Runtime.EcsSystemBundle` row and updates the canonical frame-loop description.)
- [x] Update [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md) if this removes the ECS transform activation retirement blocker. (The "FrameGraph activation of the promoted `RegisterSystem` from a simulate-phase bundle (deferred...)" gap is replaced with the active `RegisterPromotedEcsSystemBundle` ownership statement.)
- [x] Refresh `docs/api/generated/module_inventory.md` for the new `Extrinsic.Runtime.EcsSystemBundle` module surface.

## Acceptance criteria
- [x] Runtime fixed-step execution updates dirty ECS world matrices through the promoted `TransformHierarchy` path before render extraction. (Engine wires the bundle in the substep loop before `Core::ExecuteRenderFrameContract`; layering test asserts the call site; contract tests verify world matrices propagate after `Compile`/`Execute`.)
- [x] The activation path is runtime-owned and introduces no `ecs -> runtime`, graphics, platform, or app dependency edge. (`Runtime.EcsSystemBundle` lives in `src/runtime/`; `tools/repo/check_layering.py --root src --strict` passes.)
- [x] Tests prove current pass/fail state for runtime ECS scheduling rather than relying on legacy ECS compatibility tests. (New `RuntimeEcsSystemBundle.*` contract tests + `RuntimeEngineLayering.RunFrameRegistersPromotedEcsSystemBundleBetweenSimTickAndCompile` are the proof; no legacy `ECS::Systems::Transform` tests are referenced.)

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicECSTests
ctest --test-dir build/ci -L 'runtime|ecs' --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding runtime, graphics, platform, or app imports to `src/ecs`.
- Moving GPU residency or render extraction ownership into ECS.
- Hiding transform scheduling inside app/sandbox-only code.
- Combining this wiring task with unrelated ECS component refactors.
