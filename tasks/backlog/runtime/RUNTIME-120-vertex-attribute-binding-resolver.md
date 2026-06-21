---
id: RUNTIME-120
theme: B
depends_on: []
maturity_target: CPUContracted
---
# RUNTIME-120 — Reusable vertex attribute binding resolver

## Goal
- Add a reusable, CPU-tested runtime helper that resolves a named geometry
  property to a logical GPU vertex channel (position / normal / texcoord /
  color / tangent / custom) with fail-closed diagnostics, and route the mesh
  packer's normal and texcoord reads through it without behavior change.

## Non-goals
- No new GPU vertex layout, no shader changes, no color vertex channel upload
  (owned by RUNTIME-121).
- No declarative `VertexLayout` descriptor and no unification of the three
  packers (owned by RUNTIME-122).
- No editor UI to choose arbitrary property sources (owned by RUNTIME-123).
- No per-channel dirty-range / partial upload (owned by RUNTIME-124).
- No change to position validation policy (position keeps its hard-fail
  `NonFinitePosition` contract in the mesh packer for this slice).

## Context
- Owning subsystem/layer: `src/runtime` (`runtime` -> all lower layers). The
  resolver imports `Geometry.Properties` only and is GPU-agnostic.
- This is Slice 1 of the CPU->GPU vertex-attribute overhaul. Today each geometry
  kind has its own packer (`Runtime.MeshGeometryPacker`,
  `Runtime.GraphGeometryPacker`, `Runtime.PointCloudGeometryPacker`) with the
  source property names (`v:position`, `v:texcoord`, `v:normal`) inlined and a
  fixed AoS vertex struct. There is no shared "bind property X as channel Y"
  primitive, no vertex color channel in the geometry stream, and no way to bind
  an arbitrary property as normals/colors. Color today only exists as a separate
  graphics-layer `VisualizationConfig` override, not the structural vertex
  stream.
- This task adds the missing primitive without changing GPU behavior, so later
  slices have one fail-closed resolver to build on. The distinction from
  `Graphics.Component.VisualizationConfig` (sci-vis colormap overlays) is
  intentional: this resolver feeds the *structural* vertex stream.

## Slice plan (CPU->GPU vertex-attribute overhaul)
- **Slice 1 — RUNTIME-120 (this task).** Add `Extrinsic.Runtime.VertexAttributeBinding`
  with `VertexChannel`, `AttributeSourceType`, `VertexAttributeBinding`,
  `AttributeBindStatus`, `AttributeBindResult`, and `ResolveVec3Channel` /
  `ResolveVec2Channel` / `ResolveColorChannelPackedUnorm8`. CPU contract tests.
  Wire the mesh packer's normal + texcoord reads through it, behavior-preserving.
  Defers color upload, layout descriptor, UI, and partial upload to later slices.
- **Slice 2 — RUNTIME-121.** Carry a per-vertex color channel (`v:color`) through
  the mesh upload via the resolver and wire `PtrVertexAttr` from it. Operational
  on Vulkan-capable hosts.
- **Slice 3 — RUNTIME-122.** Introduce a declarative `VertexLayout` descriptor
  (AoS interleave + SoA BDA offsets), unify the three packers on it, and unify
  property naming (`v:position` everywhere; document the `v:point` graph alias
  migration).
- **Slice 4 — RUNTIME-123.** Sandbox EditorUI "bind any feasible property as
  normals / colors" control built on the resolver.
- **Slice 5 — RUNTIME-124.** Per-channel dirty tracking and partial GPU uploads
  so a normal-only edit re-uploads only the normal stream.

## Required changes
- [x] Add `src/runtime/Runtime.VertexAttributeBinding.cppm` exporting
      `Extrinsic::Runtime` channel/source/status enums, `VertexAttributeBinding`
      descriptor, `AttributeBindResult` diagnostics, and `DebugNameFor*` helpers.
- [x] Add `src/runtime/Runtime.VertexAttributeBinding.cpp` with the resolver
      bodies: whole-property fail-closed handling (empty name / missing / type
      mismatch / count mismatch) plus per-element finite/degeneracy fallback,
      optional vec3 renormalization, and `unpackUnorm4x8`-compatible color
      packing.
- [x] Register both files in `src/runtime/CMakeLists.txt`.
- [x] Route `Runtime.MeshGeometryPacker` normal and texcoord reads through
      `ResolveVec3Channel` / `ResolveVec2Channel`, preserving the existing
      normalize-or-`+Z` and finite-or-zero behavior; keep `v:position`
      hard-fail validation inline.

## Tests
- [x] Add `tests/contract/runtime/Test.VertexAttributeBinding.cpp` covering:
      missing property, type mismatch, count mismatch, empty binding,
      `AllowFallback=false` non-population, per-element non-finite fallback,
      vec3 renormalization + degenerate fallback, and color packing round-trip.
- [x] Register the new test in `tests/CMakeLists.txt` runtime contract list.
- [x] Keep `Test.MeshGeometryPacker.cpp` passing unchanged (behavior preserved).

## Docs
- [x] Add a short "Vertex attribute binding" subsection to `src/runtime/README.md`
      describing the resolver, its fail-closed contract, and how it differs from
      `VisualizationConfig`.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] List RUNTIME-120..124 in `tasks/backlog/runtime/README.md`.

## Acceptance criteria
- [x] The resolver is reusable across geometry kinds (imports `Geometry.Properties`
      only, no ECS/GPU dependency) and is exercised by the mesh packer.
- [x] Every fail-closed branch returns a precise `AttributeBindStatus` and
      populates `AttributeBindResult` counters.
- [x] Mesh packer output is byte-identical for fixtures with/without
      `v:normal` and `v:texcoord` (behavior preserved).
- [x] Focused runtime contract tests and structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'VertexAttributeBinding|MeshGeometryPacker' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding GPU/RHI/shader dependencies to the resolver module.
- Changing mesh packer output bytes for existing fixtures.

## Maturity
- Target: `CPUContracted`. The resolver is CPU/null-safe and contract-tested;
  the mesh packer is its first real consumer, so this is not a dead scaffold.
- `Operational` GPU consumption of new channels (color) is owned by `RUNTIME-121`;
  no `Operational` follow-up is owed by this slice.
