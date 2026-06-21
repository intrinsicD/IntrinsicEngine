---
id: RUNTIME-124
theme: B
depends_on: [RUNTIME-122]
maturity_target: Operational
---
# RUNTIME-124 — Per-channel dirty tracking and partial GPU uploads

## Goal
- Re-upload only the changed vertex channel(s) when geometry attributes change,
  instead of re-packing and re-uploading the entire vertex buffer on every
  `DirtyVertexAttributes` tag.

## Non-goals
- No topology (index) streaming changes.
- No GPU compaction policy changes.
- No new visible channels.

## Context
- Owning subsystem/layer: `src/runtime` extraction and `src/graphics/renderer`
  `GpuWorld`.
- Today any attribute dirty tag triggers a full `PackMesh` + full vertex-buffer
  upload (`Runtime.MeshGeometryPacker.cpp`), so a normal-only edit re-uploads
  positions and texcoords too. With the RUNTIME-122 declarative layout, channel
  sub-ranges/offsets are known, enabling per-channel partial writes.

## Required changes
- [ ] Track which channels changed (per-channel dirty bits) at the extraction
      boundary.
- [ ] Add a `GpuWorld` partial-upload path writing only the changed channel's
      bytes (and its upload->read barrier) for a resident geometry.
- [ ] Fall back to full upload when topology/vertex count changed.

## Tests
- [ ] CPU contract test: a normal-only change marks only the normal channel dirty
      and plans a partial upload; a vertex-count change forces full upload.
- [ ] Opt-in `gpu;vulkan` smoke proving partial upload produces correct shading.

## Docs
- [ ] Update `src/graphics/renderer/README.md` and `src/runtime/README.md`.
- [ ] Add a benchmark note if a perf claim is made (see benchmark protocol).

## Acceptance criteria
- [ ] A normal-only edit uploads only the normal stream; correctness is
      preserved; full upload still covers topology/count changes.
- [ ] Default-gate contract tests pass; the GPU smoke is cited for `Operational`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'MeshGeometryExtraction|GpuWorld' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Operational: cite a ci-vulkan gpu;vulkan smoke run here.
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Claiming a perf win without a baseline comparison (benchmark protocol).

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` elsewhere.
- The CPU planning contract closes `Scaffolded -> CPUContracted`; `Operational`
  owned by `RUNTIME-124` via the cited `gpu;vulkan` smoke.
