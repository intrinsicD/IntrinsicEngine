---
id: RUNTIME-199
theme: F
depends_on: []
maturity_target: Retired
---
# RUNTIME-199 — Retire dormant spatial-debug adapter registry

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

- [ ] Re-run and record the zero-production source/registration census; do not
      treat tests or future tasks as consumers.
- [ ] Remove opaque `SpatialDebugBinding` keys and app controls that can select
      a provider which production never registered.
- [ ] Delete `ISpatialDebugAdapter`, `SpatialDebugAdapterRegistry`,
      registration methods, all concrete wrapper objects, and
      `Runtime.SpatialDebugClosestFace` after the renderer packet/pass and
      lower-layer query coverage is confirmed independent of them.
- [ ] Update `RUNTIME-189` to own an orientation-specific copied record/encoder
      when it lands rather than treating either this retired registry or a
      universal immediate-debug seam as its owner.

## Tests

- [ ] Preserve renderer debug packet/pass contracts independently; delete
      runtime tests that only construct otherwise unconsumed adapters.
- [ ] Remove registry-only and closest-face-only tests; retain geometry query
      coverage in the owning geometry layer.
- [ ] Structural tests prove no adapter interface, opaque binding, or
      closest-face production module remains.

## Docs

- [ ] Update spatial-debug/runtime extraction docs with typed snapshot
      ownership and list the actually composed producers.
- [ ] Regenerate module inventory and remove registry/UI instructions.
- [ ] Refresh task indexes, session brief, and retirement records.

## Acceptance criteria

- [ ] Runtime/app exposes no spatial-debug provider selector or binding because
      no provider is currently composed.
- [ ] Registry/interface/opaque binding, every adapter implementation, and
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
