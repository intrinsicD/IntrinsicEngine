---
id: RUNTIME-124
theme: B
depends_on: [RUNTIME-122]
maturity_target: Operational
---
# RUNTIME-124 — Per-channel dirty tracking and partial GPU uploads

## Completion
- Retired on 2026-06-24 at maturity `Operational`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: ECS now has fine-grained vertex-channel dirty tags; runtime maps
  resident mesh, graph, and point-cloud edits to channel update masks; and
  graphics writes only changed contiguous SoA channel ranges while falling back
  to full uploads for topology, vertex-count, and storage-layout changes.
- Evidence: focused CPU extraction/GpuWorld/dirty-tag/editor/render-extraction
  tests, the default `ci` `IntrinsicTests` build, structural validators, and
  opt-in `ci-vulkan` runtime sandbox smoke
  `RuntimeSandboxAcceptanceGpuSmoke.VertexColorDirtyChannelPartiallyUploadsAndShadesDeferredSurface`
  passed.

## Goal
- Re-upload only the changed vertex channel(s) when geometry attributes change,
  instead of re-packing and re-uploading the entire vertex buffer on every
  `DirtyVertexAttributes` tag — for meshes, graphs, and point clouds.

## Non-goals
- No topology (index) streaming changes.
- No GPU compaction policy changes.
- No new visible channels.

## Context
- Owning subsystem/layer: `src/runtime` extraction and `src/graphics/renderer`
  `GpuWorld`.
- Today any attribute dirty tag triggers a full re-pack + full vertex-buffer
  upload through `GpuWorld::UploadGeometry` (`Runtime.MeshGeometryPacker.cpp`,
  and identically for the graph/point-cloud packers), so a normal-only or
  color-only edit re-uploads positions and texcoords too.
- **Depends on RUNTIME-122 SoA storage.** With AoS, a channel's bytes are
  scattered one-per-stride and cannot be written as a contiguous range; partial
  streaming only becomes possible once RUNTIME-122 stores each channel as its own
  contiguous sub-range with a per-channel BDA.
- The device primitive already exists: `RHI::IDevice::WriteBuffer(handle, data,
  size, offset)` supports arbitrary sub-range writes
  (`RHI.Device.cppm:136-137`). The gap is upload-side: `GpuWorld` writes the
  whole vertex block and tracks a single per-buffer
  `PendingManagedVertexUploadBarrier` (`Graphics.GpuWorld.cpp:268-276`,
  `1221-1246`) — this task adds per-channel writes and per-channel barrier
  tracking.

## Required changes
- [x] Track which channels changed (per-channel dirty bits) at the extraction
      boundary, for all three geometry kinds.
- [x] Add a `GpuWorld` partial-upload path that writes only the changed channel's
      contiguous SoA sub-range via `WriteBuffer(channelBDA, data, size, offset)`
      and records a per-channel upload->read barrier for a resident geometry.
- [x] Fall back to full upload when topology/vertex count changed.

## Tests
- [x] CPU contract test: a normal-only change marks only the normal channel dirty
      and plans a partial upload; a vertex-count change forces full upload.
- [x] Opt-in `gpu;vulkan` smoke proving partial upload produces correct shading.

## Docs
- [x] Update `src/graphics/renderer/README.md` and `src/runtime/README.md`.
- [x] Add a benchmark note if a perf claim is made (see benchmark protocol).
      No performance claim is made by this task.

## Acceptance criteria
- [x] A normal-only edit uploads only the normal stream; correctness is
      preserved; full upload still covers topology/count changes.
- [x] Default-gate contract tests pass; the GPU smoke is cited for `Operational`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'MeshGeometryExtraction|GpuWorld' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Operational: cite a ci-vulkan gpu;vulkan smoke run here.
python3 tools/agents/check_task_policy.py --root . --strict
```

Completed 2026-06-24:

```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'MeshGeometryExtraction|GraphGeometryExtraction|PointCloudGeometryExtraction|GpuWorld|ECSGeometryDirtyDomains|SandboxEditorUi|RuntimeRenderExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke.VertexColorDirtyChannelPartiallyUploadsAndShadesDeferredSurface' -L 'gpu' -L 'vulkan' --timeout 120
cmake --build --preset ci --target IntrinsicTests
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Claiming a perf win without a baseline comparison (benchmark protocol).

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` elsewhere.
- The CPU planning contract closes `Scaffolded -> CPUContracted`; `Operational`
  owned by `RUNTIME-124` via the cited `gpu;vulkan` smoke.
