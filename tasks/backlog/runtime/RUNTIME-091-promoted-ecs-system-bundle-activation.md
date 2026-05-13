# RUNTIME-091 — Activate promoted ECS system bundle in fixed-step runtime

## Goal
- Register or invoke promoted ECS systems from runtime fixed-step composition so `Extrinsic.ECS.System.TransformHierarchy` runs deterministically before render extraction without requiring app-specific manual wiring.

## Non-goals
- No new ECS component semantics.
- No graphics/RHI residency changes.
- No scene serialization, editor UI, or physics integration.
- No migration or deletion of legacy ECS modules.

## Context
- Owner/layer: `runtime`; runtime owns composition and may depend on `ecs`, `core`, `graphics`, and lower layers.
- Source review: [`docs/reviews/2026-05-13-src-ecs-gap-analysis.md`](../../../docs/reviews/2026-05-13-src-ecs-gap-analysis.md).
- `HARDEN-061` promoted `Extrinsic.ECS.System.TransformHierarchy::{OnUpdate,RegisterSystem}` but intentionally deferred runtime activation.
- `Runtime.Engine` already drives fixed-step `OnSimTick` and a CPU `Core::FrameGraph`; this task decides the runtime-owned bundle/wiring path for baseline ECS systems.
- ECS must not import runtime; activation belongs here, not in `src/ecs`.

## Required changes
- [ ] Define the promoted runtime ECS system bundle surface or explicit invocation path for fixed-step systems.
- [ ] Ensure `TransformHierarchy` runs after gameplay/app fixed-step mutations and before `Runtime.RenderExtractionCache::ExtractAndSubmit` observes world matrices.
- [ ] Preserve app extensibility: applications must still be able to add their own fixed-step passes without bypassing the baseline ECS update order.
- [ ] Add diagnostics or assertions for failed CPU frame-graph registration/compilation if the selected path uses `RegisterSystem`.
- [ ] Record any intentionally unregistered ECS systems as non-goals or follow-up tasks.

## Tests
- [ ] Add or update `tests/integration/runtime/Test.RuntimeEcsSystemBundle.cpp` or equivalent runtime integration coverage proving dirty transforms are propagated during a headless frame before extraction.
- [ ] Cover at least one hierarchy child case where a parent dirty transform updates the child world matrix through runtime-owned scheduling.
- [ ] Keep tests CPU/headless and label them `integration;runtime` or `integration;runtime;ecs` using existing label policy.

## Docs
- [ ] Update `src/ecs/Systems/README.md` to replace the deferred-activation note with the factual runtime-owned activation path.
- [ ] Update `src/runtime/` or `docs/architecture/runtime.md` documentation if a new runtime bundle API is introduced.
- [ ] Update [`docs/migration/nonlegacy-parity-matrix.md`](../../../docs/migration/nonlegacy-parity-matrix.md) if this removes the ECS transform activation retirement blocker.

## Acceptance criteria
- [ ] Runtime fixed-step execution updates dirty ECS world matrices through the promoted `TransformHierarchy` path before render extraction.
- [ ] The activation path is runtime-owned and introduces no `ecs -> runtime`, graphics, platform, or app dependency edge.
- [ ] Tests prove current pass/fail state for runtime ECS scheduling rather than relying on legacy ECS compatibility tests.

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
