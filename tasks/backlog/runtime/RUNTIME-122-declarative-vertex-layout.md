---
id: RUNTIME-122
theme: B
depends_on: [RUNTIME-120]
maturity_target: Operational
---
# RUNTIME-122 — SoA vertex channel storage and packer unification

## Goal
- Store GPU vertex data as separate-array-per-channel (SoA) sub-ranges driven by
  one declarative `VertexLayout`, unify the mesh / graph / point-cloud packers on
  it, and update the active GpuScene vertex shaders to read each channel from its
  own buffer-device-address — the prerequisite that makes per-channel partial
  uploads (RUNTIME-124) possible across all three geometry kinds.

## Non-goals
- No per-channel dirty-range *upload* path yet (owned by RUNTIME-124); this task
  lays out SoA storage so that streaming becomes a sub-range `WriteBuffer`.
- No new visible channels beyond those already packed plus the RUNTIME-121 color
  channel.
- No fragment-stage shading algorithm changes (vertex fetch only).

## Context
- Owning subsystem/layer: `src/runtime` packers, `src/graphics/renderer`
  (`GpuWorld`, `GpuGeometryRecord` fill), `src/graphics/rhi` (`GpuGeometryRecord`
  in `RHI.Types.cppm`), and the active GpuScene vertex shaders.
- **Confirmed current state (AoS).** GPU vertices are stored interleaved
  Array-of-Structures: `GpuWorld` keeps one contiguous `VertexByteOffset` /
  `VertexByteCount` / `VertexStride` block per geometry
  (`Graphics.GpuWorld.cpp:47-66`), and the *active* GpuScene path reads a packed
  struct `PackedVertex { px,py,pz,u,v,nx,ny,nz }` through the geometry record's
  single `VertexBufferBDA` (`assets/shaders/deferred/gbuffer.vert:24-78`,
  `Pass.Forward.Surface.cpp:36-41`). The `surface.vert` SoA-via-push-constant
  path (`PtrPositions`/`PtrNormals`/`PtrAux`) is **legacy/dormant** and not used
  by the default recipe.
- **Why SoA.** With AoS a single attribute's bytes are scattered one-per-stride,
  so a channel cannot be overwritten as a contiguous range — partial streaming is
  impossible. SoA makes each channel a contiguous sub-range with its own BDA.
- `RHI::IDevice::WriteBuffer(handle, data, size, offset)` already supports
  arbitrary sub-range writes (`RHI.Device.cppm:136-137`), so once storage is SoA
  the streaming primitive in RUNTIME-124 is a per-channel offset write + barrier.
- Graph and point-cloud packers currently pack **position only** (no normal /
  color fields) into 20-byte AoS structs (`Runtime.GraphGeometryPacker.cpp:83`,
  `Runtime.PointCloudGeometryPacker.cpp:75`); SoA + RUNTIME-121 is what lets them
  carry resolver-bound normals/colors.
- Property naming: ECS `GeometrySources` packers use `v:position` for all kinds;
  the `v:point` alias only appears in the geometry-library graph container. This
  task keeps the ECS contract on `v:position`.

## Slice plan
- **Slice A — RUNTIME-122 (CPU substrate, this slice's first commit).** Add a CPU
  `VertexLayout` + per-channel `VertexChannelStreams` built through the
  RUNTIME-120 resolver, with an `InterleaveToAoS(layout, streams)` helper that
  reproduces the current `MeshVertex` / `GraphVertex` / `PointCloudVertex` bytes
  exactly. Route all three packers through it. Output bytes stay byte-identical,
  so the GPU/shaders are untouched. Preserves the CPU gate. Defers all GPU-side
  storage and shader changes to Slice B.
- **Slice B — GPU SoA + shaders (Operational).** Add per-channel managed
  sub-ranges and per-channel BDAs to `ManagedGeometryAllocation` /
  `GpuGeometryRecord`, write each channel separately in `UploadGeometry`, and
  update `gbuffer.vert` (and the line/point vertex shaders) to read each channel
  from its own BDA. Requires a Vulkan-capable host; cite a `gpu;vulkan` smoke.

## Required changes
- [ ] Add a CPU `VertexLayout` descriptor (ordered channels, source types,
      per-channel element size/offset, stride) and a `VertexChannelStreams`
      builder that fills each channel via the RUNTIME-120 resolver.
- [ ] Add `InterleaveToAoS(...)` and route mesh / graph / point-cloud packers
      through the layout core; keep emitted bytes byte-identical (Slice A).
- [ ] Add per-channel byte sub-ranges + BDAs to `ManagedGeometryAllocation` and
      `RHI::GpuGeometryRecord`; write channels separately in
      `GpuWorld::UploadGeometry` (Slice B).
- [ ] Update `assets/shaders/deferred/gbuffer.vert` and the line/point vertex
      shaders to read each channel from its own BDA (Slice B).

## Tests
- [ ] Slice A: per-kind contract tests prove `InterleaveToAoS` output is
      byte-identical to the current packers for shared fixtures.
- [ ] Slice A: a layout round-trip test (channel → offset/stride math).
- [ ] Slice B: opt-in `gpu;vulkan` smoke proving SoA fetch renders correctly.

## Docs
- [ ] Update `src/runtime/README.md` and `src/graphics/renderer/README.md`.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] All three packers share one layout-driven core; Slice A output is
      byte-identical for existing fixtures (CPU gate green).
- [ ] Slice B stores vertices SoA with per-channel BDAs and the active shaders
      read them; the `gpu;vulkan` smoke is cited.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'GeometryPacker|VertexLayout|VertexChannelStreams' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
# Slice B: cite a ci-vulkan gpu;vulkan smoke run here.
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors in a single commit.
- Introducing unrelated feature work.
- Changing visible vertex output bytes for existing fixtures without test proof.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` elsewhere.
- Slice A closes `Scaffolded -> CPUContracted` (CPU SoA substrate, byte-identical
  AoS output). `Operational` owned by `RUNTIME-122` Slice B via the cited
  `gpu;vulkan` smoke.
