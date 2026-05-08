# GRAPHICS-032 — Minimal surface and present command recording path (planning)

## Goal
Lock down the design for the smallest visible-path command recording bodies — minimal surface (or debug-surface) pass body, minimal present pass body, render-target setup, and barrier/synchronization model — without writing the bodies. This planning slice records the recipe shape, label/flag policy, diagnostic surface, extensibility hooks, performance bounds, and CPU-mock vs Vulkan split, so that GRAPHICS-018/018R/033 do not have to retrofit the contract during implementation.

## Non-goals
- No implementation of pass bodies in this slice; no recipe registration, no barrier code, no command stream changes.
- No clustered/deferred lighting, shadows, or postprocess work (GRAPHICS-009 / 013A own those).
- No Vulkan operational-readiness changes; that planning lives in GRAPHICS-033.
- No new material / shader features beyond the GRAPHICS-031 default debug material (which is itself a planning task).
- No editor / ImGui overlay work (GRAPHICS-013C).
- No live ECS access from graphics.
- No `GpuWorld` contract expansion.

## Context
- Owner layer: `graphics` (renderer + framegraph). Backend-local recording bodies in `src/graphics/vulkan` are GRAPHICS-033 territory; this slice plans the CPU-mock contract that the backend implements against later.
- `src/graphics/renderer/Graphics.Renderer.cpp` returns `NullRenderer` today; existing renderer lifecycle tests intentionally expect surface/present passes to be skipped under non-operational devices. The minimal recipe must be opt-in by label or recipe ID so unrelated tests do not regress.
- The 2026-05-08 review (sections "Exact missing pieces / 5" and "minimal milestone plan / 3") requires a narrowly scoped acceptance test behind appropriate non-GPU/GPU labels.
- GRAPHICS-008/008Q already specifies renderpass attachment ownership, load/store, depth-write, and compare ops for prepass-on/off cases.
- GRAPHICS-013C/CQ already specifies `Pass.Present` keeping the CPU-testable fullscreen-triangle finalization form, plus the imported-backbuffer rejection-of-non-present-writes contract.
- GRAPHICS-007Q already specifies bucket/visibility policy that the surface body must consume rather than redefine.

## Design decisions to record
1. **Recipe identity.** Decide the recipe ID and label policy (suggested `FrameRecipe::MinimalDebugSurface` plus a `recipe.minimal-debug-surface` test label). Recipe must be opt-in; the default reference recipe stays unchanged so non-minimal lifecycle tests keep their current skip statuses.
2. **Pass set.** Lock the minimal pass list in execution order: `Pass.Surface.MinimalDebug` (single bucket draw using GRAPHICS-031 default material) → `Pass.Present.MinimalDebug` (fullscreen-triangle finalization). No depth prepass, no postprocess, no overlays in the minimal recipe.
3. **Render targets.** Decide attachment shape: one color attachment (HDR vs LDR — record decision and align with GRAPHICS-013AQ `SceneColorHDR/LDR` ownership), depth attachment (record whether required for the minimal triangle; recommend yes with depth-test on / depth-write on for parity with downstream recipes), no MSAA in this slice.
4. **Backbuffer integration.** Confirm finalization onto the imported backbuffer goes through `Pass.Present`'s fullscreen-triangle form per GRAPHICS-013CQ. Record explicit rejection of `vkCmdBlit/Copy` finalization paths and the rule that the imported backbuffer accepts writes only from `Pass.Present`.
5. **Barrier and synchronization model.** Enumerate the required transitions: surface color attachment write → present sample, depth attachment lifecycle, queue-family ownership rules (consume GRAPHICS-018/018R contracts; do not redefine). Record whether the minimal recipe declares these via the existing framegraph barrier inference or via explicit annotations.
6. **CPU-mock command-stream contract.** Decide the recorded command-stream shape consumed by `contract;graphics` tests: opcode list (BeginRenderPass, BindPipeline, BindDescriptorSet, BindVertexBuffer, BindIndexBuffer, DrawIndexed, EndRenderPass, Begin/EndPresent). Record the assertion vocabulary tests use (sequence match vs property match) and how it stays stable across allocator reshuffles.
7. **Diagnostic counters.** Name explicit counters: `MinimalSurfacePassExecutions`, `MinimalPresentPassExecutions`, `MinimalRecipeMissingPrerequisiteCount`. Decide where they surface (renderer diagnostics snapshot). Forbid silent skip — missing residency, material, or attachment must increment the prerequisite counter.
8. **Recipe-vs-default isolation.** Lock the rule that the minimal recipe never mutates non-minimal recipes' pass status; existing skipped-pass statuses on broader recipes remain explicit and unchanged.
9. **Extensibility surface.** Identify follow-on recipes that compose with the minimal one without modifying it: minimal-debug-line, minimal-debug-point, minimal-with-shadow, minimal-with-postprocess. None are in scope here; enumerate to confirm the design does not paint future passes into a corner.
10. **Performance bounds.** Record: recipe construction is O(1) per frame, no allocations on the steady-state recording path, command-stream size is bounded by single-bucket draw count, no GPU sync stalls beyond what GRAPHICS-018/018R prescribe. Forbid hidden allocation in pass body recording.
11. **Test seam split.** Enumerate the test layers: (i) `contract;graphics` CPU-mock acceptance test that drives recipe → snapshot → command stream → assertions, (ii) `contract;graphics` regression test that confirms non-minimal recipes still skip as before, (iii) opt-in `gpu;vulkan` smoke test that exercises the same recipe against a real device once GRAPHICS-033 lands. The Vulkan smoke is identified but stays stub/quarantine until GRAPHICS-033.
12. **Failure modes.** Enumerate: missing default material → counter + skip recipe, missing residency for the candidate → counter + skip recipe, attachment allocation failure → counter + abort frame with explicit diagnostic. None throw from the pass body.
13. **Layering audit.** Confirm: graphics owns the recipe, framegraph compiles it, runtime composes the snapshot, ECS is never read live, platform owns window/surface, backend owns swapchain. No new cross-layer imports beyond what GRAPHICS-013CQ already permits.

## Required changes
- Capture all thirteen decisions as explicit recorded answers in this task body.
- Cross-link with GRAPHICS-003 (frame recipe), GRAPHICS-007Q (buckets/visibility), GRAPHICS-008/008Q (depth/surface attachment ownership), GRAPHICS-013C/CQ (present), GRAPHICS-018 / 018R (Vulkan integration), GRAPHICS-022 (rendergraph diagnostics), GRAPHICS-030 (residency), GRAPHICS-031 (default material), and GRAPHICS-033 (Vulkan operational follow-up).
- Identify follow-up implementation children (do **not** open here):
  - **GRAPHICS-032-Impl-A** — recipe registration + label gating + diagnostic counters; no body content yet.
  - **GRAPHICS-032-Impl-B** — surface pass body command recording (CPU-mock).
  - **GRAPHICS-032-Impl-C** — present pass body command recording (CPU-mock); CPU acceptance test lands here.
  - **GRAPHICS-032-Impl-D** — opt-in `gpu;vulkan` smoke fixture (stub until GRAPHICS-033 lands).

## Tests
- Planning slice: validators only.
- Implementation children must add the `contract;graphics` minimal-surface acceptance test and the regression guard for non-minimal recipes; the GPU smoke stays opt-in.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/rendering-three-pass.md` with the minimal-triangle recipe entry, label policy, and diagnostic counters.
- Update `src/graphics/renderer/README.md` and `src/graphics/framegraph/README.md` with the recipe ownership and the recipe-vs-default isolation rule.
- Update `tasks/backlog/rendering/README.md` DAG between GRAPHICS-031 and GRAPHICS-033.

## Acceptance criteria
- All thirteen decisions recorded with explicit answers and trade-off rationales.
- Implementation children identified with scope and dependency gates but not opened.
- No engine behavior, no command stream additions, no recipe registration land in this slice.
- Layering invariants hold; existing renderer lifecycle tests are not regressed by this planning slice.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No silent-skip behavior in the minimal recipe; missing prerequisites must surface a named counter.
- No clustered/deferred lighting, shadows, or postprocess additions.
- No Vulkan operational-state changes (defer to GRAPHICS-033).
- No live ECS access from graphics.
- No mutation of non-minimal recipes' pass-status expectations.
- No mixing of mechanical file moves with semantic refactors.
- No premature opening of implementation child tasks before this planning slice is approved.
