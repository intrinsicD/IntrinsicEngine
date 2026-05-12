# GRAPHICS-074 — Default-recipe selection ID passes, outline pass, and picking readback drain

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
