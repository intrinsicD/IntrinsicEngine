---
id: GRAPHICS-128
theme: B
depends_on:
  - GRAPHICS-104
maturity_target: Operational
---
# GRAPHICS-128 — Object-space normal bake shared index slice

## Status

- Implementation and verification complete as of 2026-07-19; owner: Codex
  team; implementation branch: `codex/graphics-128-shared-index-slice`;
  implementation commit: `a42a57e8`.
- The branch remains active for review/integration. The graphics slice reached
  `CPUContracted` through the backend-neutral contract tests and `Operational`
  on the recorded Vulkan-capable host; the end-to-end provider remains
  `RUNTIME-129`.

## Goal
- Make the graphics-owned object-space normal texture bake record the selected
  surface slice of a shared index buffer, including a nonzero first index,
  without changing the bake's local vertex-addressing contract.

## Non-goals
- Runtime entity lookup, live `GpuWorld` plan-provider composition, import
  scheduling, cache identity, material binding, or the end-to-end runtime smoke;
  those remain `RUNTIME-129`.
- A base-vertex field, index remapping, a second index buffer, or an RHI command
  interface change.
- New bake shaders, tangent-space normal baking, or dilation behavior changes.

## Context
- Owner/layer: `graphics/renderer`; this is a narrow extension of
  `Extrinsic.Graphics.ObjectSpaceNormalTextureBake`, which retired under
  `GRAPHICS-104`.
- `GpuWorld` stores all managed surface indices in one buffer and publishes the
  selected subrange as `RHI::GpuGeometryRecord::SurfaceFirstIndex` plus
  `SurfaceIndexCount`. The bake descriptor currently carries only
  `IndexBuffer` and `IndexCount`, binds the shared buffer at byte offset zero,
  and records `DrawIndexed(IndexCount, 1, 0, 0, 0)`, so a later managed
  allocation would incorrectly draw the first allocation's indices.
- `TexcoordBufferBDA` and `NormalBufferBDA` already address the selected
  allocation's local channel arrays, while managed surface indices remain
  local vertex indices. The correct command is therefore
  `DrawIndexed(IndexCount, 1, FirstIndex, 0, 0)`: applying
  `GpuGeometryRecord::VertexOffset` as `vertexOffset` would offset the selected
  local arrays a second time. `Graphics.UvView.cpp::UvView::Record(...)` is the
  existing correct shared-index-slice precedent.

## Required changes
- [x] Add `FirstIndex` to
      `ObjectSpaceNormalTextureBakeGeometryBuffers`,
      `ObjectSpaceNormalTextureBakeGpuRecordTemplate`, and
      `ObjectSpaceNormalTextureBakeGpuRecordDesc`.
- [x] Propagate `FirstIndex` through
      `BuildObjectSpaceNormalTextureBakePlan(...)` and
      `MakeObjectSpaceNormalTextureBakeGpuRecordDesc(...)`, including every
      affected aggregate initializer.
- [x] Keep `BindIndexBuffer(IndexBuffer, 0, Uint32)` and record
      `DrawIndexed(IndexCount, 1, FirstIndex, 0, 0)`.
- [x] Keep base vertex fixed at zero; do not export a base-vertex field or
      reinterpret `GpuGeometryRecord::VertexOffset` for this bake.

## Tests
- [x] Extend
      `tests/contract/graphics/Test.ObjectSpaceNormalTextureBake.cpp` with a
      nonzero `FirstIndex` plan/descriptor/command assertion, including
      `DrawVertexOffset == 0`.
- [x] Extend
      `tests/integration/graphics/Test.ObjectSpaceNormalTextureBakeGpuSmoke.cpp`
      with one combined index buffer containing a decoy triangle before the
      target triangle; set a nonzero `FirstIndex` and prove readback samples the
      target triangle's encoded object-space normal rather than the decoy.
- [x] Preserve zero-first-index and padded-dilation coverage.

## Docs
- [x] Update `src/graphics/renderer/README.md` with the shared-index-slice and
      zero-base-vertex contract.
- [x] Regenerate `docs/api/generated/module_inventory.md` after the `.cppm`
      descriptor surface changes.

## Acceptance criteria
- [x] Plan construction and descriptor materialization preserve a nonzero
      selected surface first index.
- [x] Command recording binds the shared index buffer at byte offset zero and
      passes the selected `FirstIndex` with vertex offset zero.
- [x] An actually-run `gpu;vulkan` readback smoke distinguishes the target
      slice from preceding decoy indices.
- [x] No runtime, ECS, asset-service, Vulkan-native, or RHI interface knowledge
      is added to the graphics bake module.

## Verification

Evidence recorded on 2026-07-19:

- Strict task policy/schema, source layering, test-layout, root-hygiene,
  documentation-link, and generated-module-inventory freshness checks passed.
- The clean-workshop automated bundle passed. Manual scorecard row 3 passed:
  the added graphics exports name only existing RHI and standard scalar types;
  rows 4-6 are not applicable because this slice adds no renderer
  member/subsystem, frame-graph pass, or recipe dependency. Rows 7-8 are not
  applicable because the slice is Operational and adds no temporary exception.
- The `ci` preset configured with Clang 23; both
  `IntrinsicGraphicsContractTests` and `IntrinsicTests` built successfully.
  The focused object-space-normal-bake selector passed 17/17 tests. The full
  exclusion-only CPU selector reported 0 failures across 4,154 selected tests;
  one unrelated GLFW LeakSanitizer control test was explicitly skipped.
- The `ci-vulkan` preset configured and built `IntrinsicTests` with the required
  ASan+UBSan instrumentation. On an NVIDIA GeForce RTX 3050 with driver
  590.48.01 and Vulkan API 1.4.325, the intersected `gpu;vulkan` selector
  actually ran
  `ObjectSpaceNormalTextureBakeGpuSmoke.VulkanBakeMatchesCpuContractAtSelectedTexels`
  and passed 1/1 in 5.23 seconds with zero skips. The fixture's combined index
  buffer places a valid decoy slice before the selected nonzero-first-index
  target slice and compares the readback against their distinguishable encoded
  normals.

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -R 'ObjectSpaceNormalTextureBake' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Operational (Vulkan-capable host only):
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ObjectSpaceNormalTextureBakeGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120
```

## Forbidden changes
- Adding runtime entity or `GpuWorld` lookup to the graphics bake API.
- Passing `Vk*` types through graphics/RHI public interfaces.
- Adding a base-vertex control or offsetting both local channel BDAs and vertex
  indices.
- Expanding this prerequisite into `RUNTIME-129` provider, cache, producer, or
  material-binding work.

## Maturity
- Target: `Operational` on Vulkan-capable hosts and `CPUContracted` for the
  backend-neutral plan/command contract.
- `Operational` requires the decoy-plus-target nonzero-index
  `gpu;vulkan` readback smoke to execute successfully; the end-to-end runtime
  provider proof remains owned by `RUNTIME-129`.
