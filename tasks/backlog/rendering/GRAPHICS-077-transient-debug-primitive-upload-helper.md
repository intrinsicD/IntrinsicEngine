# GRAPHICS-077 — Backend transient-debug-primitive upload helper

## Goal
- Implement the per-frame host-visible upload helper for transient `DebugLinePacket`/`DebugPointPacket`/`DebugTrianglePacket` spans declared by `GRAPHICS-010Q`: a backend-local helper under `src/graphics/vulkan` that expands the sanitized packet spans into per-frame transient vertex/index buffers, recycled each frame, never retained on `GpuWorld` and never exposed through RHI or renderer module surfaces. Add the dedicated debug-surface overlay pass that draws debug triangles into `SceneColorHDR`/`SceneDepth` after lit composition.

## Non-goals
- No routing through retained `GpuRender_Line` / `GpuRender_Point` cull buckets (rejected per `GRAPHICS-010Q`).
- No `GpuWorld` exposure of these resources.
- No new cull buckets or frame-graph resources.
- No visualization-overlay pipeline (vector-field / isoline / Htex / UV bake — those are `GRAPHICS-078`).
- No RHI module surface change.

## Context
- Status: not started.
- Owner/layer: `graphics/vulkan` for the helper + pass; `graphics/renderer` for executor route to the new debug-surface overlay pass.
- Planning anchors: `tasks/done/GRAPHICS-010-lines-points-debug-primitives.md`, `tasks/done/GRAPHICS-010Q-transient-debug-backend-clarifications.md`.
- Today: `NullRenderer` accepts and sanitizes `DebugLinePacket`/`DebugPointPacket`/`DebugTrianglePacket` spans (`InvalidSnapshotRecordCount` is the only diagnostic), but no GPU upload helper exists; debug triangles never reach the framebuffer.
- The depth-tested vs always-on-top behavior is two pipeline variants per primitive lane (per packet `DepthTested` field), not separate cull buckets or frame-graph resources.

## Required changes
- [ ] Add a `TransientDebugUploadHelper` (backend-local under `src/graphics/vulkan`, not exported through RHI or renderer module surfaces) that:
  - allocates per-frame host-visible (transient) vertex/index buffers sized by submitted packet counts,
  - copies sanitized packet data each frame,
  - returns RHI-internal buffer views the new pass uses for draws,
  - recycles via the existing per-frame command pool / staging belt rhythm.
- [ ] Add two pipeline variants per primitive lane (DepthTested + AlwaysOnTop): three lanes (line, point, triangle) × 2 variants = 6 pipelines. Created at renderer init / `RebuildOperationalResources()`.
- [ ] Add `TransientDebugSurfacePass` (or extend `Pass.Forward.Line`/`Pass.Forward.Point` with a transient-mode entrypoint per the recorded decision; the implementer chooses the cleanest split). Pass writes `SceneColorHDR` + `SceneDepth` (LOAD-store), executes after lit composition, before postprocess.
- [ ] Add `TransientDebugUploadDiagnostics` field on the renderer diagnostics aggregate (overflow, sanitized count, recorded count). `InvalidSnapshotRecordCount` remains the only CPU diagnostic for malformed packets.
- [ ] Add executor branch routing the new pass through the recorded taxonomy.

## Tests
- [ ] `contract;graphics` test: with N debug lines + M debug points + K debug triangles per frame, the helper allocates one transient buffer set per lane and reports `TransientDebugUploadDiagnostics` matching the submitted counts.
- [ ] `contract;graphics` test: per-packet `DepthTested` correctly selects the matching pipeline variant (assert recorded `BindPipeline` calls).
- [ ] `contract;graphics` test: invalid packets (rejected by sanitizer) are not uploaded; `InvalidSnapshotRecordCount` increments.
- [ ] `contract;graphics` test: per-frame helper buffers do not leak across frames (allocator counters return to baseline).

## Docs
- [ ] Update `src/graphics/vulkan/README.md` to add the helper + diagnostics row.
- [ ] Update `src/graphics/renderer/README.md` to record the new `TransientDebugSurfacePass` ordering position.

## Acceptance criteria
- [ ] Submitted debug primitives draw into `SceneColorHDR`/`SceneDepth` deterministically.
- [ ] No retained GPU resources on `GpuWorld`; no RHI/renderer surface change.
- [ ] No regression in transient-debug rejection counters or `InvalidSnapshotRecordCount` semantics.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests ExtrinsicBackendsVulkanContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Routing transient debug through retained cull buckets.
- Retaining transient buffers on `GpuWorld`.
- Exposing the helper through RHI or renderer module surfaces.
- Adding a third pipeline variant per lane (only DepthTested + AlwaysOnTop are recorded).

## Next verification step
- Land the helper + 6 pipelines + new pass + executor route, exercise the contract tests above.
