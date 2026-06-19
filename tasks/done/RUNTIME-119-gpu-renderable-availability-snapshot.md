---
id: RUNTIME-119
theme: F
depends_on: [RUNTIME-118]
maturity_target: CPUContracted
---
# RUNTIME-119 — GPU renderable availability snapshot

## Completion
- Retired on 2026-06-19 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: `RenderExtractionCache` now exposes a read-only
  `GpuRenderableAvailabilityView` keyed by stable entity ID, with independent
  surface, edge, and point lane residency plus canonical named-buffer facts,
  without moving GPU ownership into ECS.
- Evidence: `cmake --build --preset ci --target IntrinsicRuntimeContractTests
  IntrinsicECSTests` succeeded, and focused CPU/null coverage for
  `GeometrySources`, geometry availability, packers, extraction, progressive
  data, and `SandboxEditorUi` passed 205/205 tests.

## Goal
- Expose a standard runtime-owned read-only snapshot for GPU geometry/render-lane availability, aligned with the CPU availability resolver but without storing GPU handles or renderer state in ECS.

## Non-goals
- No ECS ownership of GPU handles, buffer handles, sidecars, or renderer lifetime.
- No graphics-layer import of runtime or live ECS.
- No new Vulkan-only renderer feature or shader behavior.
- No UI migration in this task; editor consumption opens separately if needed after the snapshot exists.

## Context
- Owning subsystem/layer: `runtime` owns extraction sidecars and can present read-only test/editor models; `graphics` owns low-level GPU scene slots and buffer handles.
- Existing pieces:
  - `Graphics::Components::GpuSceneSlot` reports low-level instance/geometry registration and named buffer entries.
  - `RenderExtractionCache::RenderableSidecarView` reports test-facing mesh, graph, point-cloud, and mesh primitive-view sidecar residency.
  - Runtime extraction already owns the ECS-to-GPU residency bridge and stale-resource retirement.
- The missing standard is a single availability snapshot that answers "which GPU representation exists for this entity/lane/buffer?" in the same vocabulary as CPU source and render-lane availability.

## Required changes
- [x] Audit GPU availability information currently exposed by `GpuSceneSlot`, `GpuWorld`, `RenderExtractionCache`, and runtime bound-state inspector models.
- [x] Define a runtime-owned `GpuGeometryAvailability` or equivalent read-only model keyed by stable entity ID and render lane.
- [x] Represent surface, edge, and point lane residency independently, including instance/geometry presence, generation/slot facts, and canonical named buffer entries where available.
- [x] Map canonical buffer names (`positions`, `normals`, `edges`, `colors`, `scalars`, `sizes`, and future documented names) to lane/source availability without exposing mutable renderer ownership.
- [x] Preserve sidecar distinctions for mesh surface, mesh edge view, mesh vertex view, graph shared geometry, graph point lane instance, and point-cloud geometry.
- [x] Emit deterministic diagnostics for missing GPU residency, stale generation, unsupported lane, upload failure, and not-yet-extracted state.
- [x] Keep the API suitable for tests and UI inspectors while remaining read-only and frame-stable.

## Tests
- [x] Add `contract;runtime` coverage for GPU availability snapshots after mesh surface, edge, and point extraction.
- [x] Add `contract;runtime` coverage for graph edge and point lanes, including the graph point-lane instance sidecar.
- [x] Add `contract;runtime` coverage for point-cloud point residency.
- [x] Add stale/removal coverage proving lane removal and source invalidation are reflected in the snapshot after extraction cleanup.
- [x] Add named-buffer coverage for canonical position/edge/color/scalar/size entries that already exist in promoted extraction paths.

## Docs
- [x] Update `src/runtime/README.md` with the GPU availability snapshot contract and its relationship to CPU source availability.
- [x] Reviewed existing graphics named-buffer documentation; no
  `src/graphics/renderer/README.md` change was needed because the snapshot
  reports the existing `GpuSceneSlot` canonical entries without adding new
  graphics-owned names.
- [x] Update `tasks/backlog/runtime/README.md` if snapshot scope or dependencies change.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening or re-gating this task.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [x] Runtime has one documented read-only way to inspect GPU availability by selected entity and render lane.
- [x] GPU availability uses the same lane vocabulary as CPU/render availability (`Surface`, `Edges`, `Points`) while preserving sidecar provenance.
- [x] ECS remains CPU authoring/state only and stores no GPU handles or renderer sidecars.
- [x] Graphics remains free of live ECS/runtime imports.
- [x] Tests prove creation, reuse, removal, and stale cleanup are reflected in the snapshot.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'RenderExtraction|MeshPrimitiveViewExtraction|GraphGeometryExtraction|PointCloudGeometryExtraction|BoundRenderState' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Storing GPU handles in canonical ECS components.
- Letting graphics read live ECS or import runtime.
- Making the snapshot mutable or a new owner of GPU lifetime.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
- This task proves the read-only snapshot under runtime CPU/null contract tests. No `Operational` Vulkan follow-up is owed unless the snapshot adds backend-specific behavior beyond existing extraction evidence.
