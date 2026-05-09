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

## Recorded design decisions

Each decision below is locked for downstream implementation children. Trade-offs
are recorded so implementation can stay CPU-testable and so GRAPHICS-033 can
bring up Vulkan without changing the backend-agnostic recipe contract.

1. **Recipe identity.**
   - Decision: the opt-in recipe identity is `FrameRecipe::MinimalDebugSurface`; the stable test/diagnostic label is `recipe.minimal-debug-surface`. The label is applied only when callers explicitly select this recipe ID through frame-recipe construction or the later runtime selection seam. `DescribeDefaultFrameRecipe()` and the default reference recipe remain unchanged.
   - Rationale/rejected alternatives: reusing the default recipe was rejected because existing renderer lifecycle tests intentionally assert skip/no-op statuses under non-operational devices. A string-only recipe selector was rejected because tests need a typed identity that survives label spelling changes; the label is for diagnostics and test filtering, not dispatch.

2. **Pass set.**
   - Decision: the minimal recipe has exactly two recording passes in order: `Pass.Surface.MinimalDebug` then `Pass.Present.MinimalDebug`. The surface pass records one opaque indexed draw stream against the `SurfaceOpaque` bucket using the GRAPHICS-031 default debug surface material. The present pass records the fullscreen-triangle finalization form from GRAPHICS-013CQ. There is no depth prepass, no shadow pass, no deferred composition, no postprocess, no debug view, no selection outline, and no ImGui overlay in this recipe.
   - Rationale/rejected alternatives: keeping the list to two passes isolates the first visible-triangle path from unrelated optional systems. Routing through `DepthPrepass` was rejected for the minimal recipe because GRAPHICS-008Q already validates that depth path and adding it would hide surface-pass failures behind a second pass body.

3. **Render targets.**
   - Decision: the recipe allocates one transient color attachment, `SceneColorHDR` (`R16G16B16A16_SFLOAT`), and one transient depth attachment, `SceneDepth` (device/swapchain depth format). `SceneColorHDR` is cleared and stored by `Pass.Surface.MinimalDebug`, then sampled by `Pass.Present.MinimalDebug`. `SceneDepth` is cleared to 1.0, depth-tested with `CompareOp = Less`, depth-write enabled, stored for validation/introspection, and otherwise not sampled. MSAA is fixed at 1×.
   - Rationale/rejected alternatives: `SceneColorHDR` is used instead of `SceneColorLDR` so the surface body consumes the same forward-color ownership vocabulary as GRAPHICS-008/008Q and lets `Pass.Present` own all final LDR/backbuffer handling. Omitting depth was rejected because the downstream surface lane is depth-tested; the minimal pass should exercise the same depth attachment lifecycle even for the one-triangle milestone. MSAA was rejected to keep the first command-stream contract deterministic and allocation-free beyond canonical attachments.

4. **Backbuffer integration.**
   - Decision: imported backbuffer writes are still authorized only for the `Present` declaration (`FinalizesBackbuffer = true`). `Pass.Present.MinimalDebug` samples `SceneColorHDR` and writes the imported `Backbuffer` via the CPU-testable fullscreen-triangle form (`BindPipeline` + `Draw(3, 1, 0, 0)` after binding the present source). Backend-native `vkCmdCopyImage` / `vkCmdBlitImage` finalization is not part of the command contract.
   - Rationale/rejected alternatives: the fullscreen-triangle form preserves GRAPHICS-013CQ's format-agnostic present contract and keeps swapchain layout/color-space handling backend-local. Copy/blit finalization was rejected because graphics cannot assume identical source/backbuffer formats or a transfer-destination swapchain layout without taking platform/backend ownership.

5. **Barrier and synchronization model.**
   - Decision: the minimal recipe declares normal framegraph resource uses and relies on the existing render-graph compiler/barrier inference from GRAPHICS-003/022. Required transitions are: `SceneColorHDR` undefined/clear → color-attachment write in `Pass.Surface.MinimalDebug` → sampled/read in `Pass.Present.MinimalDebug`; `SceneDepth` undefined/clear → depth-attachment write in `Pass.Surface.MinimalDebug` → end-of-frame transient lifetime. Imported `Backbuffer` remains acquired/present-owned by the backend; the render graph authorizes only the present write and emits the same final-state packet model as other recipes. Queue-family ownership and acquire/present synchronization are consumed from GRAPHICS-018/018R/033, not redefined here.
   - Rationale/rejected alternatives: explicit hand-authored barriers in the recipe were rejected because they duplicate compiler-owned inference and would let the minimal recipe drift from default-recipes that already validate load/store and imported-resource policy.

6. **CPU-mock command-stream contract.**
   - Decision: `contract;graphics` tests assert a property-stable opcode sequence, not allocator-specific handles. The surface body sequence is `BeginRenderPass(Pass.Surface.MinimalDebug)`, `BindPipeline(DefaultDebugSurface)`, `BindDescriptorSet(SceneTable/Material)`, `BindIndexBuffer(GpuWorld.ManagedIndexBuffer)`, `DrawIndexedIndirectCount(SurfaceOpaque args/count)`, `EndRenderPass`. The present body sequence is `BeginRenderPass(Pass.Present.MinimalDebug)`, `BindPipeline(PresentFullscreenTriangle)`, `BindDescriptorSet(PresentSource = SceneColorHDR)`, `Draw(3, 1, 0, 0)`, `EndRenderPass`. Tests match opcode order, pass label, resource names, bucket kind, draw kind, and draw counts; they do not match transient allocation IDs, native handles, or command-buffer addresses.
   - Rationale/rejected alternatives: exact handle-by-handle sequence matching was rejected because transient allocator reshuffles and backend handle reuse should not break CPU/null contract tests when pass semantics are unchanged.

7. **Diagnostic counters.**
   - Decision: the renderer diagnostics snapshot gains three additive counters for this recipe family: `MinimalSurfacePassExecutions`, `MinimalPresentPassExecutions`, and `MinimalRecipeMissingPrerequisiteCount`. The two execution counters increment only after the corresponding pass records at least its begin/end body successfully. `MinimalRecipeMissingPrerequisiteCount` increments once per frame attempt when a required prerequisite is missing or invalid: default debug material/pipeline unavailable, present pipeline unavailable, `SurfaceOpaque` residency/bucket invalid, no eligible renderable selected for the minimal recipe, or attachment allocation/import authorization failure.
   - Rationale/rejected alternatives: silent skip was rejected by the task's forbidden changes and by the visible-geometry milestone. Per-prerequisite counters were deferred to implementation only if diagnostics prove ambiguous; the single missing-prerequisite counter keeps the first surface minimal while still making skips observable.

8. **Recipe-vs-default isolation.**
   - Decision: `FrameRecipe::MinimalDebugSurface` is a separate recipe construction path. It never mutates default-recipe feature gates, pass status, validation expectations, or skip/no-op behavior. Non-minimal recipes continue to report the same skipped-pass statuses under null/non-operational devices unless their own task changes them.
   - Rationale/rejected alternatives: introducing a global "minimal mode" flag on the default recipe was rejected because it would make unrelated lifecycle tests order-dependent and would conflate recipe selection with feature gating.

9. **Extensibility surface.**
   - Decision: future visible-path recipes compose alongside, not inside, this minimal recipe: `MinimalDebugLine`, `MinimalDebugPoint`, `MinimalWithShadow`, and `MinimalWithPostProcess`. Each future recipe adds its own recipe ID/label and pass list while reusing the same framegraph resource vocabulary. The `MinimalDebugSurface` pass names and diagnostics stay stable.
   - Rationale/rejected alternatives: adding optional line/point/shadow/postprocess toggles to `MinimalDebugSurface` was rejected because the two-pass recipe is the acceptance baseline; richer variants need their own contracts and tests.

10. **Performance bounds.**
    - Decision: recipe construction is O(1) per frame and declares a constant number of resources/passes. The steady-state pass recording path performs no heap allocation, no shader/pipeline creation, no material registration, no descriptor-layout creation, and no CPU/GPU synchronization beyond GRAPHICS-018/018R/033 acquire/submit/present rules. Command-stream size is O(surface draw count) with a single bucket draw source; the minimal milestone is expected to issue one indexed indirect-count draw when the reference triangle is resident.
    - Rationale/rejected alternatives: lazy first-use allocation in pass bodies was rejected because it would make the first visible frame nondeterministic and obscure whether failures are residency, material, or backend readiness problems.

11. **Test seam split.**
    - Decision: implementation children add three distinct coverage seams. First, a `contract;graphics` CPU/null acceptance test drives recipe selection → snapshot/residency setup → command-stream recording → property assertions against the opcode vocabulary in Decision 6. Second, a `contract;graphics` regression test confirms non-minimal recipes retain their previous skip/no-op statuses. Third, an opt-in `gpu;vulkan` smoke test exercises the same recipe against a real device after GRAPHICS-033 lands; it is labeled `gpu;vulkan` and excluded from the default CPU gate.
    - Rationale/rejected alternatives: relying on a GPU-only smoke was rejected because Theme A needs a deterministic CPU-supported correctness gate before operational Vulkan is guaranteed.

12. **Failure modes.**
    - Decision: pass bodies do not throw for expected missing prerequisites. Missing default material/pipeline, missing present pipeline, invalid/missing surface residency, invalid cull bucket, or unavailable eligible renderable increments `MinimalRecipeMissingPrerequisiteCount` and skips the affected recipe for that frame. Attachment allocation or imported-backbuffer authorization failure increments the same counter and aborts the frame with an explicit render-graph/renderer diagnostic. Device-lost and swapchain-acquire/present failures remain backend/runtime operational diagnostics owned by GRAPHICS-033.
    - Rationale/rejected alternatives: falling back to a copy/blit, drawing without depth, or synthesizing CPU geometry inside graphics were rejected because each would bypass the contracts already owned by GRAPHICS-013CQ, GRAPHICS-008Q, and GRAPHICS-030.

13. **Layering audit.**
    - Decision: ownership remains: graphics/renderer owns the recipe ID, pass labels, diagnostics, and CPU-mock command vocabulary; graphics/framegraph compiles declared resources, validates imported-resource writes, and emits barriers; runtime composes immutable snapshots and selects the opt-in recipe; ECS is never queried live by graphics; platform owns windows/events; `src/graphics/vulkan` owns surface/swapchain/acquire/present mechanics. The recipe consumes only `RenderWorld`/`GpuWorld` snapshots/views, asset IDs/material slots, RHI abstractions, and framegraph resources.
    - Rationale/rejected alternatives: adding a graphics-side ECS query, platform-window dependency, live `AssetService` lookup, or runtime import was rejected because each violates `AGENTS.md` layering invariants and would undermine the GRAPHICS-029..031 ownership chain.

## Required changes
- ✅ Recorded all thirteen decisions above with explicit answers and rejected-alternative rationale.
- ✅ Cross-linked the decisions with GRAPHICS-003 (frame recipe), GRAPHICS-007Q (buckets/visibility), GRAPHICS-008/008Q (depth/surface attachment ownership), GRAPHICS-013C/CQ (present), GRAPHICS-018 / 018R (Vulkan integration), GRAPHICS-022 (rendergraph diagnostics), GRAPHICS-030 (residency), GRAPHICS-031 (default material), and GRAPHICS-033 (Vulkan operational follow-up).
- ✅ Identified follow-up implementation children (do **not** open here):
  - **GRAPHICS-032-Impl-A** — add `FrameRecipe::MinimalDebugSurface`, recipe registration, `recipe.minimal-debug-surface` label gating, and the three renderer diagnostics counters. No pass-body command content. Tests: `contract;graphics` recipe introspection asserts the two-pass declaration, resource set, label, and default-recipe isolation.
  - **GRAPHICS-032-Impl-B** — implement `Pass.Surface.MinimalDebug` CPU-mock command recording against `SurfaceOpaque`, the GRAPHICS-031 default material/pipeline, `SceneColorHDR`, and `SceneDepth`. Tests: `contract;graphics` surface opcode/property assertions plus missing-prerequisite counter coverage for material/residency gaps.
  - **GRAPHICS-032-Impl-C** — implement `Pass.Present.MinimalDebug` CPU-mock command recording and the end-to-end CPU acceptance test from recipe → snapshot → command stream. Tests also assert non-minimal recipes retain current skip/no-op statuses.
  - **GRAPHICS-032-Impl-D** — add the opt-in `gpu;vulkan` smoke fixture for the same recipe after GRAPHICS-033 operational readiness lands; label `gpu;vulkan` and keep it outside the default CPU gate.

## Tests
- Planning slice: validators only.
- Implementation children must add the `contract;graphics` minimal-surface acceptance test and the regression guard for non-minimal recipes; the GPU smoke stays opt-in.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- ✅ Updated `docs/architecture/rendering-three-pass.md` with the minimal-triangle recipe entry, label policy, render-target/present/barrier policy, and diagnostic counters.
- ✅ Updated `src/graphics/renderer/README.md` and `src/graphics/framegraph/README.md` with recipe ownership, command-stream/test vocabulary, and the recipe-vs-default isolation rule.
- ✅ Updated `tasks/backlog/README.md` and `tasks/backlog/rendering/README.md` DAG entries between GRAPHICS-031 and GRAPHICS-033.

## Acceptance criteria
- All thirteen decisions recorded with explicit answers and trade-off rationales. ✅
- Implementation children identified with scope and dependency gates but not opened. ✅
- No engine behavior, no command stream additions, no recipe registration land in this slice. ✅
- Layering invariants hold; existing renderer lifecycle tests are not regressed by this planning slice. ✅

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

## Completion
- Completed: 2026-05-09.
- Commit reference: planning-only slice in this working tree; no code/test/CMake/shader changes landed.
- Notes:
  - The task was retired from `tasks/backlog/rendering/` to `tasks/done/` after recording the decisions and doc mirrors.
  - Implementation children GRAPHICS-032-Impl-A/B/C/D are identified but explicitly **not** opened. Impl-A is unblocked once this planning slice is approved; Impl-B depends on Impl-A and GRAPHICS-031-Impl-A material/pipeline availability; Impl-C depends on Impl-B; Impl-D depends on GRAPHICS-033 operational readiness.
