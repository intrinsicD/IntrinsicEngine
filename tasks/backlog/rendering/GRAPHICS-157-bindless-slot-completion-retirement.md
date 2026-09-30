---
id: GRAPHICS-157
theme: B
depends_on: []
maturity_target: CPUContracted
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: filed from an independent review finding (Codex 6 Astra, 2026-09-30, RUNTIME-292 re-review); the fix owes the contract test below and a validation-layer note.
contract_schema: 1
contracts: []
contract_review: no catalog contract covers descriptor-heap lifetime; `graphics.recipe-slot-lookup` is about render-recipe slots, not bindless descriptors. Amend `docs/architecture/graphics.md` (bindless heap section) with the retirement rule when this lands.
---
# GRAPHICS-157 — Completion-based retirement of freed and replaced bindless slots

## Goal
- `IBindlessHeap::FreeSlot` and `UpdateTextureSlot` queue a descriptor write that
  `FlushPending` applies at the start of the next frame, after only the reused frame slot's
  fence was waited. An earlier frame still in flight (frames-in-flight distance, or an
  immediate compute/readback submission ordered on the graphics queue) may still sample the
  old descriptor. `UPDATE_AFTER_BIND` permits writing descriptors of a bound set, but not
  descriptors that a pending submission reads (VUID-vkUpdateDescriptorSets-None-03047).
  Example: `Graphics.ImGuiOverlaySystem.cpp` font-atlas replacement resets the old texture
  lease, whose slot is freed and rebound while the previous frame's ImGui draw may still run.
- Retire freed and replaced slots by completion, not by the next flush: a freed or replaced
  slot's old descriptor stays valid until every submission that could read it has completed
  (the frames-in-flight window since the last flush that exposed it, plus any immediate
  submit recorded while it was exposed), and only then is the write applied and the slot
  index reusable. The renderer already flushes once per frame in `BeginFrame` (RUNTIME-292);
  that call stays and becomes the retirement drain.
- The texture lease that backs a freed slot must outlive the retirement as well
  (`TextureManager` release is frame-deferred today; align its deferral with the same
  completion rule).

## Acceptance criteria
- [ ] Contract test with the mock heap (`tests/support/MockRHI.hpp`): a slot freed or
      replaced in frame N keeps its old binding through `FlushPending` calls until
      `GetGlobalFrameNumber() - N > GetFramesInFlight()` (and until any registered immediate
      submit token completed), then reverts / rebinds and becomes allocatable again.
- [ ] The ImGui font-atlas replacement and any other `FreeSlot`/`UpdateTextureSlot` caller
      in `src/graphics/renderer` go through the retirement rule (no direct rebinding of a
      slot that a pending frame reads).
- [ ] Validation-layer note: run the sandbox and the `gpu;vulkan` suite with validation
      enabled while replacing the font atlas (DPI or font change) and record in the task
      whether `VUID-vkUpdateDescriptorSets-None-03047` (descriptor in use by pending command
      buffers) fires before and after the fix; the fix must leave the suite free of that VUID.
- [ ] `docs/architecture/graphics.md` documents the bindless retirement rule.

## Verification
```bash
cmake --build build/ci -j"$(nproc)" --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'Bindless' --timeout 60
cmake --build build/ci-vulkan -j"$(nproc)"
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```

## Context
- Found during the RUNTIME-292 re-review (2026-09-30): the per-frame `FlushPending` in
  `Renderer::BeginFrame` (added so colormap LUT descriptors reach every pass without a UI
  overlay) made the pre-existing early-replacement window visible; the ImGui pass flushed the
  same queue before. The window is inherent to applying frees/updates after one frame slot's
  fence: `src/graphics/vulkan/Backends.Vulkan.Bindless.cpp` (`FlushPending`,
  `vkUpdateDescriptorSets`) and `src/graphics/renderer/Backends/Null/Backends.Null.Bindless.cpp`.
- Related lifetime rule for buffers: `Graphics.GpuPropertyResidency` retires slots by recorded
  completions (frames in flight + transfer/readback tokens), the model this task should follow.
