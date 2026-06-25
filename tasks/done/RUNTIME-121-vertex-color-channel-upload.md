---
id: RUNTIME-121
theme: B
depends_on: [RUNTIME-120]
maturity_target: Operational
---
# RUNTIME-121 — Per-vertex color channel through the geometry vertex stream

## Completion
- Retired on 2026-06-24 at maturity `Operational` on Vulkan-capable hosts
  (`CPUContracted` elsewhere).
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: Mesh `GeometrySources` now resolve count-matched `v:color` through
  `ResolveColorChannelPackedUnorm8`, upload the packed unorm8 stream through
  `GpuWorld::GeometryUploadDesc::PackedVertexColors`, publish it via
  `GpuGeometryRecord::ColorBufferBDA`, and consume it in the active
  default-recipe GpuScene surface/GBuffer shader path.
- Evidence: focused CPU packer/GpuWorld/runtime-extraction coverage and the
  opt-in `gpu;vulkan` deferred-surface smoke listed in Verification passed.

## Goal
- Carry a per-vertex `v:color` property through the active deferred GpuScene mesh
  upload as a packed unorm8 color stream consumed by the
  default-recipe deferred GBuffer shader pair
  (`assets/shaders/forward/default_debug_surface.vert` +
  `assets/shaders/deferred/default_debug_gbuffer.frag`), so a
  recomputed/imported vertex color is visible in surface shading.

## Non-goals
- No per-edge / per-face color authoring (separate follow-up).
- No colormap / scalar-field changes (owned by the `VisualizationConfig` path).
- No GPU SoA storage or shader-fetch migration (owned by RUNTIME-122).
- No editor UI for choosing the color source property (owned by RUNTIME-123).
- This slice adds the mesh color channel. Per-point / per-node color for point
  clouds and graphs rides on the RUNTIME-122 GPU SoA storage path (their 20-byte
  AoS structs have no color field today) and is a RUNTIME-122 follow-up, not
  here.

## Context
- Owning subsystem/layer: `src/runtime` (packer + extraction) and
  `src/graphics/renderer` (GpuScene geometry record / deferred shader wiring);
  shader edits land in the active default-recipe GpuScene surface/GBuffer pair.
- The active default renderer path reads mesh vertices through
  `assets/shaders/forward/default_debug_surface.vert` and
  `RHI::GpuGeometryRecord::VertexBufferBDA`; the deferred GBuffer fragment is
  `assets/shaders/deferred/default_debug_gbuffer.frag`. The older
  `surface.vert` `PtrVertexAttr` path is dormant and must not be the
  implementation target for this task.
- `RUNTIME-120` adds `ResolveColorChannelPackedUnorm8`; this task consumes that
  resolver to produce a mesh color channel for the active deferred GpuScene path.
- The `GeometryUploadDesc` currently exposes only interleaved vertex bytes; this
  task adds an interim mesh color stream carried alongside the current active
  vertex data and surfaced through the active GpuScene record/shader contract.
  `RUNTIME-122` later replaces this with uniform per-channel SoA storage for
  meshes, graphs, and point clouds.

## Required changes
- [x] Add an optional packed-color upload buffer to the mesh pack result built
      via `ResolveColorChannelPackedUnorm8` from `v:color`.
- [x] Extend `GpuWorld`/`GpuGeometryRecord` and the active deferred GpuScene
      shaders to upload, expose, and consume the per-vertex color stream without
      relying on dormant `surface.vert` push constants.
- [x] Mark/extend the extraction dirty path so a `v:color` change triggers a
      color re-upload.

## Tests
- [x] CPU contract test: packer emits count-matched packed colors when `v:color`
      is present and none when absent.
- [x] CPU integration test: `DirtyVertexAttributes` reuploads the structural
      color stream through runtime extraction.
- [x] Opt-in `gpu;vulkan` smoke proving a per-vertex color is visible in shading.

## Docs
- [x] Update `src/graphics/renderer/README.md` and `src/runtime/README.md` for the
      color channel path.
- [x] Regenerate `docs/api/generated/module_inventory.md` if surfaces change.

## Acceptance criteria
- [x] A mesh entity with a count-matched `v:color` renders with that color via
      the active deferred GpuScene path; absence falls back to material shading.
- [x] CPU contract tests pass under the default gate; the GPU smoke is cited.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'MeshGeometryPacker|GpuWorld|RuntimeRenderExtraction\.MeshVertexColorDirtyAttributeReuploadsStructuralColorStream' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests -- -j16
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke.ReferenceTriangleVertexColorStreamShadesDeferredSurface' -L 'gpu' -L 'vulkan' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Reintroducing fragment normal-texture sampling (see BUG-047).

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` elsewhere.
- The CPU packer contract closes `Scaffolded -> CPUContracted`; `Operational`
  owned by `RUNTIME-121` via the cited `gpu;vulkan` smoke in this task's
  Verification.
