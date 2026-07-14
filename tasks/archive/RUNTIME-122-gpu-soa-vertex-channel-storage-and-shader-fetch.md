---
id: RUNTIME-122
theme: B
depends_on: [RUNTIME-120, RUNTIME-121]
maturity_target: Operational
---
# RUNTIME-122 — GPU SoA vertex channel storage and shader fetch

## Completion
- Retired on 2026-06-24 at maturity `Operational` on Vulkan-capable hosts
  (`CPUContracted` elsewhere).
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: Runtime mesh, graph, point-cloud, and mesh primitive-view packers now
  emit explicit per-channel vertex streams. `GpuWorld` stores position,
  texcoord, normal, and color data as separate managed SoA channel ranges,
  publishes per-channel BDAs through `GpuGeometryRecord`, and keeps stable
  vertex element offsets for culling and draw metadata. The active GpuScene
  surface, depth, selection, line, and point vertex shaders fetch through the
  channel BDAs instead of interleaved vertex structs.
- Evidence: focused CPU packer/GpuWorld/shader-contract coverage, full
  CPU-supported CTest, structural validators, and the opt-in `gpu;vulkan`
  runtime sandbox surface plus line/point smoke listed in Verification passed.

## Goal
- Move the active GPU geometry storage and GpuScene vertex shaders from the
  current interleaved AoS fetch path to separate-array-per-channel (SoA)
  sub-ranges with one BDA per channel, using the completed CPU
  `VertexChannelStreams` substrate as the input contract.

## Non-goals
- No per-channel dirty-range *upload* path yet (owned by RUNTIME-124); this task
  lays out SoA storage so that streaming becomes a sub-range `WriteBuffer`.
- No new visible channels beyond the currently packed channels and the
  `RUNTIME-121` mesh color channel.
- No editor binding controls (owned by `RUNTIME-123`).
- No fragment-stage shading algorithm changes (vertex fetch only).

## Context
- Owning subsystem/layer: `src/runtime` packers, `src/graphics/renderer`
  (`GpuWorld`, `GpuGeometryRecord` fill), `src/graphics/rhi` (`GpuGeometryRecord`
  in `RHI.Types.cppm`), and the active GpuScene vertex shaders.
- **Pre-task state (AoS).** GPU vertices were stored interleaved
  Array-of-Structures: `GpuWorld` kept one contiguous `VertexByteOffset` /
  `VertexByteCount` / `VertexStride` block per geometry, and the active
  default-recipe GpuScene vertex shaders read packed vertex structs through the
  geometry record's single `VertexBufferBDA`. The `surface.vert`
  SoA-via-push-constant path (`PtrPositions`/`PtrNormals`/`PtrAux`) is
  **legacy/dormant** and not used by the default recipe.
- **Why SoA.** With AoS a single attribute's bytes are scattered one-per-stride,
  so a channel cannot be overwritten as a contiguous range — partial streaming is
  impossible. SoA makes each channel a contiguous sub-range with its own BDA.
- **Completed precursor.** The CPU substrate already exists:
  `Extrinsic.Runtime.VertexChannelStreams` defines the `VertexLayout` and
  interleave helper that reproduced the pre-SoA `MeshVertex`, `GraphVertex`,
  and `PointCloudVertex` bytes under CPU contract tests. RUNTIME-122 consumed
  that substrate for GPU-side storage, packer upload, shader fetch, docs, and
  opt-in Vulkan proof.
- `RHI::IDevice::WriteBuffer(handle, data, size, offset)` already supports
  arbitrary sub-range writes (`RHI.Device.cppm:136-137`), so once storage is SoA
  the streaming primitive in RUNTIME-124 is a per-channel offset write + barrier.
- **Pre-task graph/point-cloud state.** Graph and point-cloud packers packed
  **position only** (no normal / color fields) into 20-byte AoS structs
  (`Runtime.GraphGeometryPacker.cpp:83`, `Runtime.PointCloudGeometryPacker.cpp:75`);
  SoA + RUNTIME-121 is what let them carry resolver-bound channels without
  extending those legacy AoS structs.
- Property naming: ECS `GeometrySources` packers use `v:position` for all kinds;
  the `v:point` alias only appears in the geometry-library graph container. This
  task keeps the ECS contract on `v:position`.
- Storage model is fixed by ADR-0022: uniform SoA with per-channel streaming; the
  AoS fast lane is deferred to RUNTIME-125 (profile-gated). See
  `docs/adr/0022-vertex-storage-soa-per-channel-streaming.md`.

## Slice plan
- **This task — GPU SoA + shaders.** Add per-channel managed sub-ranges and
  per-channel BDAs to `ManagedGeometryAllocation` / `GpuGeometryRecord`, route
  packer output through `VertexChannelStreams` into those channel ranges, and
  update `gbuffer.vert` plus line/point vertex shaders to read each channel from
  its own BDA. Requires CPU/null contract coverage plus a cited `gpu;vulkan`
  smoke on a Vulkan-capable host.

## Required changes
- [x] Route mesh / graph / point-cloud packers to build `VertexChannelStreams`
      via the RUNTIME-120 resolver and upload per channel.
- [x] Add per-channel byte sub-ranges + BDAs to `ManagedGeometryAllocation` and
      `RHI::GpuGeometryRecord`; write channels separately in
      `GpuWorld::UploadGeometry`.
- [x] Update the active default-recipe surface/depth/selection/line/point vertex
      shaders, plus `assets/shaders/deferred/gbuffer.vert`, to read each
      channel from its own BDA.

## Tests
- [x] CPU/null contract tests proving packers build channel streams and
      `GpuWorld` records channel ranges/BDAs without changing existing fallback
      behavior.
- [x] Opt-in `gpu;vulkan` smoke proving SoA fetch renders correctly.

## Docs
- [x] Update `src/runtime/README.md` and `src/graphics/renderer/README.md`.
- [x] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [x] All three geometry kinds store active GPU vertex data as per-channel SoA
      ranges with published channel BDAs.
- [x] Active GpuScene shaders read position, texcoord, normal, and available
      color data from the channel BDAs.
- [x] Default CPU gate remains green and the `gpu;vulkan` smoke is cited for
      `Operational`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'GeometryPacker|MeshPrimitiveView|VertexLayout|VertexChannelStreams|GpuWorld|RenderPassContract_GpuScene' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests -- -j16
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke\.(ReferenceTriangleVertexColorStreamShadesDeferredSurface|ReferenceTriangleMeshConfiguredLineWidthAndPointDrawLanesPresent)' -L 'gpu' -L 'vulkan' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors in a single commit.
- Introducing unrelated feature work.
- Changing visible vertex output bytes for existing fixtures without test proof.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` elsewhere.
- The completed CPU `VertexChannelStreams` substrate is the prerequisite. This
  task owns the remaining GPU SoA storage/shader path and its cited
  `gpu;vulkan` operational proof.
