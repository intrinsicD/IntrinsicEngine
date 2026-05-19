# GRAPHICS-074 — Default-recipe selection ID passes, outline pass, and picking readback drain

## Status

- State: in-progress (Slice A landing).
- Owner/agent: local agent workflow.
- Branch: `claude/setup-agentic-workflow-mf8d0`.
- Activated: 2026-05-19 — first unblocked Theme A leaf after GRAPHICS-073 retirement and the GRAPHICS-072 deferred-lighting + shadow-atlas binding completion (recent commits `b55a285`, `08dd7d1`, `a3ab39b`).
- Next verification step: `cmake --preset ci && cmake --build --preset ci --target IntrinsicGraphicsContractTests && ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.

## Slice plan

The full scope (5 selection-ID pipelines + outline pipeline + host-visible
`Picking.Readback` buffer + readback drain + `PublishPickResult`/`PublishNoHit`
wiring + four contract tests) does not fit a single reviewable patch. The
plan mirrors the GRAPHICS-072/073 slice shape — each slice preserves the
CPU/null gate and only the final slice exercises the readback drain.

- **Slice A (this slice; EntityId selection pipeline + `"PickingPass"` executor route + GpuScene-aware shader pair).** Add `m_SelectionEntityIdPass` instance + `m_SelectionEntityIdPipelineLease` to `NullRenderer`; emplace the pass before the operational publisher; create the EntityId selection pipeline in `InitializeOperationalPassResources(device)` with the standard reset/republish pattern; route the recipe's `"PickingPass"` executor branch through `RecordSelectionEntityIdPass(...)` with the recorded `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy; expose `GetSelectionEntityIdPipeline()` / `GetSelectionEntityIdPipelineDesc()` on `IRenderer`; reset the pass/lease in `Shutdown()`. Author the GpuScene-aware `selection/entity_id.{vert,frag}` shader pair that pairs the existing GpuScene vertex-fetch chain with two R32_UINT color outputs (`EntityId` = `inst.EntityID`, `PrimitiveId` = `EncodeSelectionId(Entity, 0)`). Pipeline depth state: depth-test off, depth-write off, `DepthTargetFormat::Undefined`. `BuildDefaultFrameRecipe` orders `PickingPass` *before* `DepthPrepass` and the picking declaration is color-only, so a depth-equal / depth-prepass-on shape (matching the forward / deferred GBuffer pipelines) would be render-pass-incompatible *and* would depth-test against an uninitialized buffer — producing incorrect IDs or consistent no-hit readbacks once Slice D's drain lands. Contract test: `EntityIdPickingPipelineSurvivesOperationalRebuild` asserts the byte-identical descriptor across `RebuildOperationalResources()`. Adds no `Picking.Readback` buffer and no drain (Slice D scope); does not touch the other three ID pipelines or the outline pipeline (Slices B / C).
- **Recipe-side follow-up for depth-sorted picking (gates correctness once Slice D lands).** Reorder `BuildDefaultFrameRecipe` so the `addOrderedPass("PickingPass", ...)` block runs *after* the `EnableDepthPrepass` block (and gates on `EnableDepthPrepass`), and add `builder.Read(depth, TextureUsage::DepthRead)` to the picking-pass declaration. Then `BuildSelectionEntityIdPipelineDesc()` can flip back to `DepthOp::Equal` / `DepthTargetFormat::D32_FLOAT` / `DepthTestEnable=true,DepthWriteEnable=false` so picking samples the nearest-surface ID instead of last-fragment-wins. The Face/Edge/Point selection pipelines added by Slice B follow the same shape as whatever this descriptor settles on; the outline pipeline (Slice C) is unaffected by this reorder. Originally raised in review of Slice A.
- **Slice B (follow-up; Face/Edge/Point selection ID pipelines + executor branch fan-out).** Add `m_SelectionFaceIdPass` / `m_SelectionEdgeIdPass` / `m_SelectionPointIdPass` instances + their pipeline leases. Extend the `"PickingPass"` executor route to dispatch the requested kind based on `PickRequest`/`SelectionSystem` state (or follow whichever sub-pass-ordering decision `Pass.Selection.*` Execute signatures already encode). Author or wire `selection/face_id.{vert,frag}`, `selection/edge_id.{vert,frag}`, `selection/point_id.{vert,frag}` GpuScene-aware shader pairs, each writing the `EncodeSelectionId(domain, payload)` value into `PrimitiveId` per `GRAPHICS-012Q`. Tests: pipeline-survives-rebuild for each kind.
- **Slice C (follow-up; outline pipeline + `"SelectionOutlinePass"` executor route).** Add `m_SelectionOutlinePass` instance + lease; pipeline is a fullscreen quad (vertex `post_fullscreen.vert` + fragment `selection_outline.frag`); add the executor branch routing `SelectionOutlinePass::Execute(...)` against the recipe's outline target. Tests: outline pipeline survives rebuild; outline executor records when at least one selectable entity is present.
- **Slice D (follow-up; `Picking.Readback` buffer allocation + drain + `PublishPickResult`/`PublishNoHit` wiring).** Allocate the host-visible `Picking.Readback` buffer (sized `4 bytes * frames-in-flight * (1 EntityId + 1 PrimitiveId word)` per `GRAPHICS-013AQ`'s histogram-readback drain pattern); record the per-frame copy from the requested pixel(s) of `EntityId` and `PrimitiveId` into the buffer; drain on `BeginFrame()` after the issuing frame's fences complete via timeline-semaphore comparison; route the decoded result through `SelectionSystem::PublishPickResult(...)` for valid samples and `PublishNoHit(...)` for `EntityId == 0` / invalidated requests / readback failures. Tests: (1) hit pixel inside triangle drives `PublishPickResult` with the matching `StableEntityId`; (2) miss outside the triangle drives `PublishNoHit`; (3) Vulkan-only smoke (`gpu;vulkan`) opt-in if and when GRAPHICS-033D's host-buffer pattern extends to picking readback.

## Goal
- Wire the existing `Pass.Selection.{EntityId,PointId,EdgeId,FaceId}` and `Pass.Selection.Outline` classes into the renderer executor under the default recipe, allocate the host-visible `Picking.Readback` buffer, implement the readback drain on `BeginFrame()` after the issuing frame's fences complete, and route `SelectionSystem::PublishPickResult` / `PublishNoHit` per `GRAPHICS-012`/`012Q`.

## Non-goals
- No transparent / special-forward picking eligibility (`GRAPHICS-025` planning).
- No editor selection policy (runtime/editor owns it per `GRAPHICS-012Q`).
- No transform-gizmo hit testing (that lives in `RUNTIME-014` `GizmoInteraction`).
- No ECS mutation from graphics.

## Context
- Status: not started.
- Owner/layer: `graphics/renderer` for pass routing + readback drain; `RHI::BufferManager` for the host-visible buffer.
- Planning anchors: `tasks/done/GRAPHICS-012-picking-selection-outline.md`, `tasks/done/GRAPHICS-012Q-picking-backend-runtime-clarifications.md`.
- Today: All five `Pass.Selection.*` `.cpp` files exist as shells; `SelectionSystem` exposes `RequestPick`/`ConsumePick`/`PublishPickResult`/`PublishNoHit`; the executor lambda has no branch for any selection pass; no `Picking.Readback` buffer is allocated.
- The `RenderFrameInput::PickPixelRequest` span is the runtime-side input; one pick request per frame keys the readback.

## Required changes
- [ ] Add `m_SelectionEntityIdPipelineLease`, `m_SelectionPointIdPipelineLease`, `m_SelectionEdgeIdPipelineLease`, `m_SelectionFaceIdPipelineLease`, `m_SelectionOutlinePipelineLease` and matching pass instances to `NullRenderer`.
- [ ] In `InitializeOperationalPassResources(device)`, create the four selection-ID pipelines (`pick_id.{vert,frag}`, `pick_mesh.{vert,frag}`, `pick_line.{vert,frag}`, `pick_point.{vert,frag}` per `GRAPHICS-012` decisions) and the outline pipeline (`selection_outline.frag` + a fullscreen vertex).
- [ ] Allocate the `Picking.Readback` buffer (host-visible, sized for `4 bytes * frames-in-flight` per request kind, mirroring the `GRAPHICS-013AQ` histogram-readback drain pattern).
- [ ] Add executor branches `"Pass.Selection.EntityId"`, `"…PointId"`, `"…EdgeId"`, `"…FaceId"`, `"Pass.Selection.Outline"` routing through `RecordSelection*Pass(...)` helpers with the recorded `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy.
- [ ] On `BeginFrame()`, drain `Picking.Readback` for the issuing frame after its fences complete (timeline-semaphore comparison) and call `SelectionSystem::PublishPickResult(...)` for valid samples, `PublishNoHit(...)` for `EntityId == 0` / invalidated requests / readback failures.

## Tests
- [ ] `contract;graphics` test: with one extracted procedural surface and a `PickPixelRequest{ pixel inside triangle, kind = EntityId }`, after one frame the readback drain calls `PublishPickResult` with `EntityId == StableEntityId(triangleEntity)`.
- [ ] `contract;graphics` test: with a pick request outside the triangle, `PublishNoHit` is called.
- [ ] `contract;graphics` test: outline pass records when at least one selectable entity is present.
- [ ] `contract;graphics` test: pipelines + buffer survive `RebuildGpuResources()`.

## Docs
- [ ] Update `src/graphics/renderer/README.md` to record selection ID + outline + picking-readback as operationally wired.
- [ ] Update `docs/architecture/rendering-three-pass.md` if pass ordering shifts.

## Acceptance criteria
- [ ] All five selection passes record commands in the operational state.
- [ ] `Picking.Readback` drain produces `PublishPickResult` / `PublishNoHit` calls deterministically.
- [ ] No regression in CPU/null tests.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mutating ECS state from graphics.
- Implementing transparent picking eligibility (reserved for `GRAPHICS-025`).
- Owning runtime selection policy (runtime/editor-owned per `GRAPHICS-012Q`).

## Next verification step
- Allocate the readback buffer, create the five pipelines, wire the executor routes + drain, exercise the contract tests above.
