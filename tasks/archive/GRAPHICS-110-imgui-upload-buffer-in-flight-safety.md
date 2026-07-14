---
id: GRAPHICS-110
theme: B
depends_on: []
---
# GRAPHICS-110 — Per-frame/ring ImGui upload buffers for in-flight safety

## Completion
- Retired on 2026-07-02 at maturity `Operational` on a Vulkan-capable host.
- Implementation commit: this commit adds frame-slot partitioning for the
  ImGui, transient-debug, and visualization-overlay upload helpers, preserves
  growth/allocation diagnostics, and left retained overlay copy/upload cleanup
  to the now-retired follow-up `GRAPHICS-114`.
- Task-state commit: this retirement commit moves the task to `tasks/done/`
  and removes it from the open rendering backlog list.
- Final retirement evidence on 2026-07-02:
  `cmake --build --preset ci-vulkan --target IntrinsicTests` passed, and
  `ctest --test-dir build/ci-vulkan --output-on-failure -R
  'KMeans|ImGuiSurfaceGpuSmoke|TransientDebugSurfaceGpuSmoke|VisualizationOverlaySurfaceGpuSmoke|RuntimeSandboxAcceptanceGpuSmoke\.(ClickPickReadbackSelectsReferenceTriangleAndBackgroundClears|HierarchySelectionKeepsDefaultSandboxVisibleWithOutline|InspectorTransformEditShiftsReferenceTrianglePixels|ReferenceTriangleScalarFieldColormapResolvesOnLineAndPointLanes)'
  -L 'gpu' -L 'vulkan' --timeout 180` passed 13/13 tests. The default CPU gate
  also passed 3459/3459 tests after the implementation.

## Goal
- Make `Extrinsic.Graphics.ImGuiUploadHelper` (and, if they share the pattern,
  the sibling transient-debug / visualization-overlay upload helpers) safe for a
  multi-frame-in-flight renderer by partitioning the host-visible upload storage
  per frame-in-flight (or ring-offsetting within a single buffer) so a new
  frame's `WriteBuffer` can never overwrite vertices/indices that an
  earlier-in-flight frame's GPU draw is still reading.

## Non-goals
- No change to the ImGui overlay draw-data contract, `Pass.ImGui` recording, or
  the `ImGuiOverlayFrame` payload shape.
- No change to the CPU-side flatten/copy in `Runtime.ImGuiAdapter` or
  `Graphics.ImGuiOverlaySystem`; retained overlay copy/upload cleanup was owned
  by the follow-up `GRAPHICS-114`.
- Not a rewrite of `RHI::BufferManager`; reuse its leasing API.

## Context
- Owning subsystem/layer: `graphics/renderer` (`graphics -> core`, asset IDs,
  `graphics/rhi`). No ECS/runtime/app knowledge.
- Opened by the `UI-030` `ImGuiUploadHelper` buffer-ownership audit
  (2026-07-01). Current state (`src/graphics/renderer/Graphics.ImGuiUploadHelper.cpp`):
  the helper owns exactly one growing vertex buffer (`m_VertexBuffer`) and one
  growing index buffer (`m_IndexBuffer`), both `HostVisible = true`. `BeginFrame()`
  is a no-op, and `UploadFrame(...)` writes every frame's coalesced payload at
  byte offset `0` via `device.WriteBuffer(handle, data, bytes, 0u)`, overwriting
  the previous frame's contents in place. There is no per-frame partition, no
  ring offset, and no per-buffer fence.
- Consequence: this is safe only when the renderer is effectively
  single-frame-in-flight (or otherwise guarantees the previous ImGui draw
  completed before the next `UploadFrame`). With more than one frame in flight,
  frame N's overwrite can clobber storage that frame N-1's GPU is still reading,
  because the Vulkan device `BeginFrame` frame-slot fence gates slot `N mod F`
  (frame `N-F`), not frame `N-1`, and this single buffer is shared across all
  slots.
- The helper comment states it "mirrors the renderer-owned transient-debug /
  visualization-overlay helpers"; confirm whether those share the same
  single-buffer assumption and, if so, whether they should be fixed together.

## Required changes
- [x] Determine the renderer's actual frames-in-flight count and whether the
      current single-buffer ImGui path is provably safe under it; record the
      finding.
- [x] If not provably safe, partition `ImGuiUploadHelper` storage per
      frame-in-flight (or ring-offset within one buffer keyed by the frame slot)
      so concurrent in-flight frames never share the same written range;
      surface the frame-slot/ring index through `BeginFrame(...)` or the upload
      call.
- [x] Keep `GetBufferAllocationCount()` and the overflow/growth behavior intact;
      grow per-slot capacity independently or share a capacity policy that does
      not reintroduce the hazard.
- [x] Audit the sibling transient-debug / visualization-overlay upload helpers
      for the same pattern and either fix them here or file their own follow-up.
- [x] Keep the retained/copy-reduction follow-up (`GRAPHICS-114`) blocked until
      this task defines the safe per-frame/ring upload storage model it will
      build on.

## Tests
- [x] Extend/keep the default-gate contract tests for the upload helper (offsets,
      overflow, growth, allocation count) with the per-frame/ring model.
- [x] Add an opt-in `gpu;vulkan` smoke (or a deterministic multi-slot simulation)
      proving two in-flight frames use non-overlapping written ranges.

## Docs
- [x] Update `src/graphics/renderer/README.md` (or the ImGui pass/upload docs) to
      describe the per-frame/ring upload-buffer model.
- [x] Link `GRAPHICS-114` as the follow-up for reducing steady-state ImGui
      overlay copy/upload overhead.

## Acceptance criteria
- [x] Two frames in flight provably never share a written vertex/index range in
      the ImGui upload path (test or documented single-frame-in-flight proof).
- [x] `GRAPHICS-114` is the named follow-up for retained overlay transport and
      CPU copy/upload reductions.
- [x] `python3 tools/repo/check_layering.py --root src --strict` passes; the
      helper stays graphics/RHI-only.
- [x] Default CPU-gate upload-helper contract tests pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ImGuiUpload' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
# On a Vulkan-capable host:
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'ImGui' --timeout 120
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding ECS/runtime/app imports into the graphics upload helper.

## Maturity
- Target: `Operational` on Vulkan-capable hosts (the opt-in `gpu;vulkan` smoke
  proves non-overlapping in-flight ranges); `CPUContracted` for the
  backend-neutral per-frame/ring contract tests.
- Current implementation state (2026-07-02): `Operational`. Deterministic
  multi-slot contract coverage is implemented for ImGui, transient-debug, and
  visualization-overlay upload helpers, and targeted `gpu;vulkan` ImGui,
  transient-debug, visualization-overlay, and sandbox smokes passed on the
  Vulkan-capable host.
