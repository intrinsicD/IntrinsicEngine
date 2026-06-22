---
id: BUG-051
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-051 — Mesh color visualization lacks automatic property-buffer extraction

## Goal
- Make mesh `glm::vec4` color properties, including `v:color`, available to
  promoted surface shading when `VisualizationConfig` requests a per-element
  color buffer.

## Non-goals
- No SoA vertex-layout migration and no `GpuGeometryRecord` BDA expansion
  (owned by RUNTIME-122).
- No structural mesh vertex color stream or default material-color policy
  change (RUNTIME-121 remains open for that larger vertex-channel path).
- No editor "bind any property as channel" UI (owned by RUNTIME-123).
- No Vulkan-only behavior.

## Context
- Symptom: mesh scalar visualization can auto-emit a property-buffer upload
  from CPU `GeometrySources`, but mesh color-buffer visualization does not have
  the equivalent automatic path. A `VisualizationConfig::PerVertexBuffer` using
  `v:color` can therefore require an explicit adapter binding instead of being
  available from the preserved mesh property.
- Expected behavior: if a mesh surface visualization selects a count-matched
  finite `glm::vec4` property, runtime extraction emits the color property
  buffer and the existing `GpuEntityConfig::ColorBDA` shader path can consume
  it.
- Impact: imported or generated vertex colors are preserved in CPU properties
  but are not reliably wired into promoted surface color visualization.
- Owner: `src/runtime` extraction; graphics already owns residency and shader
  consumption for visualization property buffers.

## Completion
- Completed: 2026-06-22. Commit/PR: this local fix commit.
- Root cause: runtime extraction only auto-appended mesh scalar visualization
  property buffers from `GeometrySources`; `glm::vec4` color-buffer
  visualizations still required an explicit adapter binding even when the mesh
  property was present and count-matched.
- Fix summary: render extraction now auto-emits mesh color visualization
  property-buffer packets from vertex/edge/face `GeometrySources`, fails closed
  for missing or unsupported sources, and graphics sync now derives
  `GpuEntityConfig::VisDomain` from the selected per-element color source.

## Required changes
- [x] Add a mesh color visualization property-buffer extraction path matching
      the existing mesh scalar path.
- [x] Preserve existing explicit visualization adapter bindings and packet
      stats.

## Tests
- [x] Add runtime extraction coverage proving `PerVertexBuffer("v:color")`
      emits a color packet and property-buffer upload without an explicit
      adapter binding.
- [x] Add a missing/invalid color-source regression to prove fail-closed
      diagnostics remain deterministic.

## Docs
- [x] Update runtime docs for automatic mesh color property-buffer extraction.
- [x] Record final status in this task and the retirement log.

## Acceptance criteria
- [x] A mesh with a finite count-matched `glm::vec4` `v:color` property can be
      surfaced through `VisualizationConfig::PerVertexBuffer` under the default
      CPU gate.
- [x] Missing or unsupported color properties do not emit stale/invalid GPU
      packets and increment diagnostics.
- [x] Fix does not introduce layering violations.

## Verification
```bash
cmake --preset ci
# Passed.

cmake --build --preset ci --target IntrinsicRuntimeGraphicsCpuTests -- -j4
# Passed.

ctest --test-dir build/ci --output-on-failure -R 'RuntimeRenderExtraction.MeshColorVisualizationPropertyBuffer' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 2/2 tests.

cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests -- -j4
# Passed.

ctest --test-dir build/ci --output-on-failure -R 'GraphicsMinimalAcceptance\.VisualizationSyncWritesEquivalentLinePointColorSourceConfig' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 1/1 test.

cmake --build --preset ci --target IntrinsicTests -- -j4
# Passed.

ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 4980/4980 tests.

python3 tools/repo/check_layering.py --root src --strict
# Passed.

python3 tools/agents/check_task_policy.py --root . --strict
# Passed after retirement.
```

## Forbidden changes
- Shipping a fix without a regression test when one is feasible.
- Changing default imported-mesh material color policy.
- Expanding the RHI/GpuWorld geometry record in this bug slice.

## Maturity
- Target: `CPUContracted`.
- The shader-visible `ColorBDA` path already exists; this task closes the
  CPU/null extraction gap. No `Operational` follow-up is owed for this adapter
  wiring, while structural vertex colors stay owned by RUNTIME-121/RUNTIME-122.
