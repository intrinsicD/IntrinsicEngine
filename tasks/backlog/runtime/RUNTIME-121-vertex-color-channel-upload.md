---
id: RUNTIME-121
theme: B
depends_on: [RUNTIME-120]
maturity_target: Operational
---
# RUNTIME-121 — Per-vertex color channel through the geometry vertex stream

## Goal
- Carry a per-vertex `v:color` property through the mesh upload as a packed
  unorm8 color stream and bind it to the surface vertex shader's `PtrVertexAttr`,
  so a recomputed/imported vertex color is visible in shading.

## Non-goals
- No per-edge / per-face color authoring (separate follow-up).
- No colormap / scalar-field changes (owned by the `VisualizationConfig` path).
- No declarative vertex layout descriptor (owned by RUNTIME-122).
- No editor UI for choosing the color source property (owned by RUNTIME-123).

## Context
- Owning subsystem/layer: `src/runtime` (packer + extraction) and
  `src/graphics/renderer` (geometry record / push-constant wiring); shaders in
  `assets/shaders`.
- `surface.vert` already declares `PtrVertexAttr` (per-vertex packed ABGR colors,
  `unpackUnorm4x8`) but the mesh packer never produces a color stream;
  `RUNTIME-120` adds `ResolveColorChannelPackedUnorm8` which this task consumes.
- The `GeometryUploadDesc` currently exposes only interleaved vertex bytes; this
  task adds an optional color buffer carried alongside and surfaced as a BDA.

## Required changes
- [ ] Add an optional packed-color upload buffer to the mesh pack result built
      via `ResolveColorChannelPackedUnorm8` from `v:color`.
- [ ] Extend `GpuWorld`/geometry record to upload and expose the per-vertex color
      BDA, and bind it to `PtrVertexAttr` for the surface pass.
- [ ] Mark/extend the extraction dirty path so a `v:color` change triggers a
      color re-upload.

## Tests
- [ ] CPU contract test: packer emits count-matched packed colors when `v:color`
      is present and none when absent.
- [ ] Opt-in `gpu;vulkan` smoke proving a per-vertex color is visible in shading.

## Docs
- [ ] Update `src/graphics/renderer/README.md` and `src/runtime/README.md` for the
      color channel path.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if surfaces change.

## Acceptance criteria
- [ ] A mesh entity with a count-matched `v:color` renders with that color via
      `PtrVertexAttr`; absence falls back to material shading.
- [ ] CPU contract tests pass under the default gate; the GPU smoke is cited.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'MeshGeometryPacker|GpuWorld' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Operational: cite a ci-vulkan gpu;vulkan smoke run here.
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
