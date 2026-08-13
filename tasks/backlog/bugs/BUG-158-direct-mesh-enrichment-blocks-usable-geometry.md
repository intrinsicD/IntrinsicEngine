---
id: BUG-158
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence]
contract_review: "The repair changes runtime/UI method readiness while optional UV/normal enrichment may write property-backed mesh state. It must reuse canonical element-domain preflight and preserve generation-safe property publication without narrowing compatible sources."
maturity_target: Operational
---
# BUG-158 — Optional direct-mesh enrichment blocks already usable geometry

## Goal

- Make an imported mesh visible, selectable, and geometry-method-ready as soon
  as its geometry-only materialization is published, while UV/texture
  enrichment continues independently with truthful progress and stale-result
  rejection.

## Non-goals

- No UV atlas algorithm or performance change (`BUG-159` and `BUG-160`).
- No removal of authored/generated UVs, normal computation, texture baking, or
  progressive status.
- No method-specific readiness exception or UI-only bypass.

## Context

- Symptom: the import executor already publishes
  `BuildRuntimeHalfedgeMeshGeometryOnly(...)`, but
  `Runtime.EditorWorkspaceSnapshots.Models.cpp` returns before resolving any
  geometry-processing entries whenever `AssetImportMeshEnrichmentState` is
  queued/running.
- Expected behavior: optional presentation enrichment may be pending without
  making the canonical geometry source unavailable. Only an operation whose
  semantic inputs are actually missing should be disabled.
- Impact: on UV-less meshes, a multi-second or pathological atlas job makes
  loading feel slower than Framework24 and prevents curvature or other
  geometry work even though topology and positions are resident.
- Existing source-signature validation must drop enrichment if a user method
  changes the mesh before the job publishes; the repair must test that race
  rather than weakening it.

## Control surfaces

- Config: unchanged; enrichment policy remains on the existing import recipe.
- UI: pending enrichment remains visible, but method buttons use canonical
  per-operation readiness rather than a blanket return.
- Agent/CLI: the same runtime operation preflight becomes available at the same
  geometry-only boundary.

## Required changes

- [ ] Remove the blanket geometry-processing model return on active direct-mesh
      enrichment and derive each action from its real canonical inputs.
- [ ] Keep enrichment status/progress/diagnostics visible while actions are
      available.
- [ ] Prove a geometry mutation while enrichment is pending invalidates the
      stale enrichment publication instead of overwriting the user's result.
- [ ] Preserve base-geometry visibility, selection, focus, dirty-state, and
      failure behavior when enrichment later succeeds, fails, or is cancelled.

## Tests

- [ ] Add a contract regression with a blocked enrichment job asserting
      curvature and compatible geometry actions are available immediately.
- [ ] Add a race regression that mutates the mesh before completion and asserts
      stale UV/normal enrichment cannot replace the newer property/topology
      generation.
- [ ] Add or extend promoted-Vulkan smoke evidence that the geometry-only mesh
      remains rendered while enrichment is pending.
- [ ] Default CPU and opt-in `gpu;vulkan` gates stay green.

## Docs

- [ ] Update the import-progress/readiness prose to distinguish base geometry
      readiness from optional presentation enrichment.
- [ ] Update the product scorecard evidence when the live workflow passes.

## Acceptance criteria

- [ ] A UV-less imported mesh can run curvature before atlas completion.
- [ ] Enrichment progress remains truthful and no completed job clobbers a
      newer user mutation.
- [ ] No method-specific bypass or duplicate availability rule is introduced.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'DirectMeshEnrichment|SandboxEditorMeshMethods|AssetImport' --timeout 120
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 120
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes

- No waiting for atlas completion on the main/editor thread.
- No disabling all geometry actions from one presentation-enrichment bit.
- No publishing a stale enrichment result after canonical geometry changes.
