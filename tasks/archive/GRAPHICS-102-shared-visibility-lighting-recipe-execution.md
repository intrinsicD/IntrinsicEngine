---
id: GRAPHICS-102
theme: B
depends_on: [GRAPHICS-099, GRAPHICS-101]
maturity_target: CPUContracted
---
# GRAPHICS-102 — Shared visibility and lighting recipe execution

## Goal
- Implement CPU-tested shared recipe execution for renderer-neutral visibility,
  grouping, lighting, and environment products over scoped snapshot data.

## Non-goals
- No final backend command buffers from shared recipes.
- No Vulkan-specific culling, shadow-map, probe, or acceleration-structure
  build implementation.
- No UI editing or loadable schema changes beyond consuming `GRAPHICS-101`
  validated config values.

## Context
- Owning subsystem/layer: graphics renderer-level recipe execution contracts.
  The shared recipes produce renderer-neutral products consumed by compatible
  renderers through frame recipes.
- Accepted design: visibility/grouping and lighting/environment are shared
  configurable recipe layers usable by all renderers, while renderers retain
  final interpretation.

## Required changes
- [x] Add visibility/grouping recipe execution that outputs visible item sets,
      rejected item diagnostics, grouping keys, batch groups, instance groups,
      LOD selections, spatial partitions, ordering hints, and optional
      acceleration-structure build requests.
- [x] Add lighting/environment recipe execution that resolves authored lights,
      emissive geometry, environment maps, probes, volumes, tags, quality
      settings, shadow/probe/GI intents, debug modes, and fallbacks into
      renderer-neutral products.
- [x] Add compatibility checks so renderers declare which products they consume.
- [x] Add diagnostics for rejected items, unsupported products, fallback use,
      stale inputs, and degraded outputs.

## Tests
- [x] Add CPU contract tests for visibility filtering, grouping keys, LOD
      selection, rejected-item diagnostics, lighting fallback behavior, and
      renderer compatibility checks.
- [x] Add empty-scene and unsupported-product fail-closed cases.

## Docs
- [x] Document shared visibility/grouping and lighting/environment product
      contracts in `src/graphics/renderer/README.md`.
- [x] Cross-link this task from `tasks/backlog/rendering/README.md`.

## Acceptance criteria
- [x] Shared recipes output renderer-neutral products, never backend command
      buffers.
- [x] Renderers can opt into supported products and receive deterministic
      diagnostics for unsupported ones.
- [x] CPU contract tests cover normal, empty, unsupported, and degraded cases.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'VisibilityRecipe|GroupingRecipe|LightingRecipe|EnvironmentRecipe' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Producing backend command buffers or Vulkan resources from shared recipe
  execution.
- Letting shared recipes mutate project data.

## Maturity
- Target: `CPUContracted`. Shared recipe execution is CPU-tested and
  renderer-neutral.
- `Operational` owned by `GRAPHICS-103`.

## Completion
- Retired on 2026-06-24 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: Added `Extrinsic.Graphics.SharedRenderRecipeExecution`, a CPU-only
  shared recipe execution module that consumes immutable `RenderWorld` data and
  scoped `SnapshotEnvelope` metadata to produce renderer-neutral visibility,
  grouping, lighting, and environment products.
- Visibility execution now emits visible item sets, rejected-item diagnostics,
  grouping keys, batch groups, instance groups, LOD selections, spatial
  partitions, and optional acceleration-structure build requests without
  allocating backend resources or recording command buffers.
- Lighting/environment execution resolves authored lights, emissive geometry
  identities, environment map/probe/volume/tag/quality products,
  shadow/probe/GI intents, debug modes, and deterministic fallbacks.
- Compatibility checks let renderers declare consumed shared products and
  report missing renderer capabilities or missing produced products with
  structured diagnostics.
- Added graphics contract tests in
  `tests/contract/graphics/Test.SharedRenderRecipeExecution.cpp` for normal,
  empty, malformed, unsupported, stale/degraded, fallback, and compatibility
  cases.
- Evidence: `cmake --preset ci`; `cmake --build --preset ci --target
  IntrinsicTests -- -j16`; focused
  `VisibilityRecipe|GroupingRecipe|LightingRecipe|EnvironmentRecipe` CTest
  filter; full CPU-supported CTest gate; structural validators listed in
  Verification.
