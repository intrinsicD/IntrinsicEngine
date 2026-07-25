---
id: RUNTIME-199
theme: F
depends_on: []
maturity_target: Retired
---
# RUNTIME-199 — Retire dormant spatial-debug adapter registry

## Status

- Completed and retired on 2026-07-25 at `Retired`.
- Commit reference: this retirement commit records the census, deletion, ADR
  amendment, and documentation sync.
- Zero-production-registration census re-run at implementation time:
  `RegisterSpatialDebugAdapter` had **no production call site anywhere in
  `src/`** — the only seven call sites were in
  `tests/integration/runtime/Test.RuntimeRenderExtraction.cpp`. The Sandbox
  nevertheless shipped "Enable BVH debug" / "Clear debug" controls that authored
  an `ECS::Components::SpatialDebugBinding` whose `RegistryKey` no production
  adapter could ever resolve, so the control could only ever increment
  `SpatialDebugMissingAdapterCount`. Tests and the future `RUNTIME-189` were not
  treated as consumers.
- Deleted: `ISpatialDebugAdapter`, `BvhAdapter`, `KdTreeAdapter`, `OctreeAdapter`,
  `ConvexHullAdapter`, `SpatialDebugAdapterRegistry`, `SpatialDebugSnapshotBatch`,
  `Extrinsic.Runtime.SpatialDebugClosestFace`, the
  `ECS.Component.SpatialDebugBinding` component, the extraction
  register/unregister/count/registry-for-test surface and `ExtractSpatialDebug`
  pump, thirteen `RuntimeRenderExtractionStats` counters, the editor
  undo/redo command, the selected-model cache signature contribution, the
  scene-serialization unsupported-entity counter, the scene-document copy
  entry, and the two Sandbox UI control sites. No replacement abstraction,
  snapshot variant, or immediate-debug service was introduced.
- Preserved deliberately: the frozen graphics `SpatialDebugVisualizers`
  packet/pass contract and the `RuntimeRenderSnapshotBatch::SpatialDebug*`
  spans, which remain declared and default-empty until a real producer exists.
  Geometry BVH/KD-tree/octree/convex-hull query correctness coverage stays in
  the owning geometry layer and was not touched.
- `RUNTIME-189` already owns an orientation-specific copied snapshot and
  explicitly forbids an adapter registry or opaque binding, so no re-gating was
  required; ADR-0008 was **amended** (per its own amendment clause) rather than
  silently superseded.
- Verification evidence:
  - `cmake --preset ci` + `cmake --build --preset ci --target IntrinsicTests`
    completed a full 1287-target build;
  - the default CPU gate passed **4236/4236** in 61.97 s
    (`-LE 'gpu|vulkan|slow|flaky-quarantine'`, one skip:
    `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` on this headless
    host). The 4264 → 4235 delta is exactly the 29 deleted dead-surface cases;
    the +1 back to 4236 is the new
    `RuntimeEngineLayering.RetiredSpatialDebugRegistryHasNoProductionSurface`
    structural test, which pins the deletion (no adapter interface, registry,
    opaque ECS binding, or closest-face module) while asserting the preserved
    graphics packet contract is still declared;
  - strict layering (746 files, 6695 references, 0 violations), doc-link,
    docs-sync, test-layout, and root-hygiene checks passed; the module
    inventory regenerated to 388 modules with no spatial-debug entry.

## Goal

- Delete the unused opaque spatial-debug adapter registry/binding path, its
  unconsumed BVH/KD-tree/octree/hull adapter family, and the test-only
  closest-face public module without adding a replacement abstraction.

## Non-goals

- No universal `SpatialDebugSnapshot` variant, immediate-mode debug-draw
  service, or renderer plugin registry.
- No ownership of geometry acceleration structures in graphics.
- No change to geometry query algorithms or the existing renderer debug
  primitive packet/pass contracts.

## Context

- `Runtime.SpatialDebugAdapters` exports an interface and opaque-key registry;
  production has no registration site, while tests construct registrations
  directly. ECS can author a key for which no production provider exists.
- BVH/KD-tree/octree/hull conversion is useful only when a live feature owns
  the source. The source audit found no production registration site; tests
  alone do not justify preserving all four conversions as engine API.
- A future real feature owns its own copied record and pure conversion to the
  existing graphics debug packets. For example, `RUNTIME-189` may add an
  orientation-specific snapshot when that feature lands; this task does not
  prebuild it.
- `Runtime.SpatialDebugClosestFace` likewise has test consumers but no
  production workflow. A dead public seam must not be preserved as a presumed
  future extension.

## Slice plan

- **Slice A — deletion proof.** Reconfirm the zero-production-consumer census,
  pin the existing graphics packet/pass destination, and move any lower-layer
  query correctness assertions out of runtime-wrapper tests.
- **Slice B — cleanup.** Delete registry/interface/opaque ECS bindings,
  tree/hull adapters, closest-face production surface, registration methods,
  and registration-only tests/CMake entries in one reviewable mechanical
  slice.

## Required changes

- [x] Re-run and record the zero-production source/registration census; do not
      treat tests or future tasks as consumers.
- [x] Remove opaque `SpatialDebugBinding` keys and app controls that can select
      a provider which production never registered.
- [x] Delete `ISpatialDebugAdapter`, `SpatialDebugAdapterRegistry`,
      registration methods, all concrete wrapper objects, and
      `Runtime.SpatialDebugClosestFace` after the renderer packet/pass and
      lower-layer query coverage is confirmed independent of them.
- [x] Update `RUNTIME-189` to own an orientation-specific copied record/encoder
      when it lands rather than treating either this retired registry or a
      universal immediate-debug seam as its owner.

## Tests

- [x] Preserve renderer debug packet/pass contracts independently; delete
      runtime tests that only construct otherwise unconsumed adapters.
- [x] Remove registry-only and closest-face-only tests; retain geometry query
      coverage in the owning geometry layer.
- [x] Structural tests prove no adapter interface, opaque binding, or
      closest-face production module remains.

## Docs

- [x] Update spatial-debug/runtime extraction docs with typed snapshot
      ownership and list the actually composed producers.
- [x] Regenerate module inventory and remove registry/UI instructions.
- [x] Refresh task indexes, session brief, and retirement records.

## Acceptance criteria

- [x] Runtime/app exposes no spatial-debug provider selector or binding because
      no provider is currently composed.
- [x] Registry/interface/opaque binding, every adapter implementation, and
      closest-face dead surfaces are deleted; no generic snapshot or
      immediate-debug service replaces them.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SpatialDebug|RenderExtraction|ClosestFace' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- A new debug-draw manager, registry, service, or borrowed geometry-tree
  pointer.
- Keeping a test-only production module for hypothetical future use.
- Deleting query correctness coverage from the geometry owner.

## Maturity

- Target: `Retired`; closure is the verified deletion of the zero-consumer
  runtime surface while existing renderer packet/pass tests remain green.
