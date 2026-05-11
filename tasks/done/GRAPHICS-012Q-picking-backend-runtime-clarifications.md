# GRAPHICS-012Q — Picking backend/runtime clarification follow-ups

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-06 after `GRAPHICS-011Q` retirement cleared `tasks/active/`.
- Completed: 2026-05-06.
- Branch: `claude/agentic-workflow-session-vQDOA`.
- Implementation commit: `f931a7b` (resolve decisions and sync rendering-three-pass / graphics / renderer-README docs).
- Task-state commit: pending retirement commit (this commit moves the file from `tasks/active/` to `tasks/done/`).
- Resolution: decisions recorded below and consequential notes synced into `docs/architecture/rendering-three-pass.md` (picking and sub-element selection contract + picking notes block), `docs/architecture/graphics.md` (`SelectionSystem` ownership bullet in the GPU scene ownership block), and `src/graphics/renderer/README.md` (matching ownership-contract bullet next to `DebugViewSystem` and `ImGuiOverlaySystem`). The rendering backlog README entry for `GRAPHICS-012Q` is redirected to the `tasks/done/` location by this retirement commit. Verification: `python3 tools/agents/check_task_policy.py --root . --strict` (75 task files validated, 0 findings) and `python3 tools/docs/check_doc_links.py --root .` (187 relative links, no broken links).

## Decisions
- **Shader-side `EntityId` and `PrimitiveId` encoding.** `EntityId` is an
  `R32_UINT` target carrying the canonical stable extracted entity ID
  surfaced through `RenderableInstance` (the same `StableEntityId`
  reported by `PickReadbackResult::StableEntityId`); the cull buckets'
  `firstInstance` indirection lets every selection ID pass write the
  authoritative stable entity ID without consulting live ECS storage.
  Value `0` is reserved for "no hit" and must never be emitted for a
  valid renderable. `PrimitiveId` is an `R32_UINT` target whose **high
  four bits encode the `SelectionPrimitiveDomain`** (`None=0`,
  `Entity=1`, `Face=2`, `Edge=3`, `Point=4`) and whose **low 28 bits
  encode the domain-local payload**, exactly mirroring
  `EncodedSelectionId::DomainShift = 28u` and the
  `DomainMask = 0xFu << DomainShift` constants in
  `Graphics.SelectionSystem.cppm`. Authoritative payload sources per
  domain:
  - `EntityIdPass` writes the stable entity ID to `EntityId` and writes
    `EncodeSelectionId(SelectionPrimitiveDomain::Entity, 0u)` into
    `PrimitiveId` so a hit with no sub-element refinement still carries
    the entity domain bits.
  - `FaceIdPass` writes `EncodeSelectionId(Face, faceIndex)` where
    `faceIndex` is the post-cull authoritative face index from the
    surface bucket's index range exposed by `RenderableInstance` (the
    same face IDs that drive CPU refinement). When an authoritative
    topology face ID is not available (helper lines or non-mesh surface
    sources), the rendered primitive index is written and CPU
    refinement remains the compatibility fallback per the
    surface/triangle policy already in `rendering-three-pass.md`.
  - `EdgeIdPass` writes `EncodeSelectionId(Edge, edgeIndex)` where
    `edgeIndex` is the line bucket's domain-local segment index from
    the same authoritative source the GPU draw uses; helper lines on
    pure-surface entities continue to fall back to CPU face-anchored
    refinement.
  - `PointIdPass` writes `EncodeSelectionId(Point, pointIndex)` where
    `pointIndex` is the point bucket's domain-local sample index.
  Backends never invert this packing: shaders pack `(domain << 28) |
  (payload & 0x0FFFFFFFu)`, and the CPU/null contract validates the
  same packing through `EncodeSelectionId` so the `EncodedSelectionId`
  helper is the single seam for pack/unpack.
- **Backend `Picking.Readback` -> `PublishPickResult` /
  `PublishNoHit` drain.** The `Picking.Readback` resource is a
  graphics-owned, host-visible buffer copied from the requested pixel(s)
  of the `EntityId` and `PrimitiveId` targets at frame-record time; it
  is **not** a synchronous stall. The renderer drains the readback on
  the next `BeginFrame()` after the issuing frame's fences complete and
  invokes `SelectionSystem::PublishPickResult(...)` for valid samples
  (decoding the `R32_UINT` words through `EncodedSelectionId` and
  populating `PickReadbackResult::StableEntityId` from the matching
  `EntityId` sample). When the requested pixel reads `EntityId == 0`,
  when the original request was invalidated (resize, frame drop), or
  when the readback fails deterministically, the renderer calls
  `SelectionSystem::PublishNoHit()` instead. The CPU/null backend
  simulates the same drain through the same seam without any
  Vulkan-specific code path, keeping the CPU/null correctness gate
  authoritative. Backends never invoke `RequestPick` or
  `ConsumePick` themselves: pending-pick consumption stays inside the
  renderer's frame-record path so `SelectionSystemDiagnostics`
  `PickRequestCount`/`PickConsumeCount`/`PickHitCount`/`PickNoHitCount`
  remain comparable across backends.
- **Runtime ownership of `StableEntityId` and outline-mask
  resolution.** `src/runtime` is the sole owner of the
  `StableEntityId -> live ECS entity` resolution, of any selection /
  hover ECS mutation, and of the selection-outline input mask. Runtime
  consumes `SelectionSystem::GetLastPickResult()` /
  `GetLastPointIdResult()` (or the equivalent extraction-side seam),
  resolves the stable ID through its sidecar maps, applies editor
  selection policy (single-select vs additive, hover vs commit, point
  picking vs entity picking), and surfaces the resulting selected /
  hovered stable IDs back through the runtime extraction batch as part
  of `SelectionSnapshot`. `SelectionOutlinePass` consumes that
  snapshot's stable IDs as the outline-mask producer; graphics never
  reads or mutates ECS state and never imports editor selection policy.
  This preserves the AGENTS.md graphics layer rules (`graphics/* -> no
  live ECS knowledge`) and the existing `Graphics.SelectionSystem`
  reporting-only contract.
- **Transparent / special-material picking eligibility.** Until
  `GRAPHICS-025` (hybrid transparent / special-material path) lands,
  picking eligibility is restricted to the eight-bucket cull contract
  documented in `rendering-three-pass.md`: `SelectionSurface`,
  `SelectionLines`, and `SelectionPoints` mirror the opaque
  `SurfaceOpaque` / `Lines` / `Points` lanes for `Selectable`
  renderables only. Transparent and special-forward renderables are
  **not** eligible for ID-pass writes; runtime extraction must surface
  them through CPU pick fallback (matching the existing CPU compatibility
  fallback policy for missing primitive hints) if editor policy
  requires picking transparent surfaces. When `GRAPHICS-025` introduces
  transparent / special-material lanes, eligibility extends through new
  selectable sub-buckets without changing the `EncodedSelectionId`
  domain/payload packing or the four-bucket selection vocabulary
  documented above.

## Resolution
- Decisions recorded above and consequential notes synced into
  `docs/architecture/rendering-three-pass.md` (PrimitiveId target row,
  picking and sub-element selection contract, picking notes block) and
  `docs/architecture/graphics.md` (GPU scene ownership picking/selection
  bullet) with a matching backend/runtime ownership bullet added to
  `src/graphics/renderer/README.md` so source readers see the same
  contract without reading architecture docs.
- Verification: `python3 tools/agents/check_task_policy.py --root . --strict`
  and `python3 tools/docs/check_doc_links.py --root .`.

## Goal
- Clarify backend shader/readback and runtime selection-resolution details after the CPU/null `GRAPHICS-012` picking and selection contracts.

## Non-goals
- No C++ behavior changes.
- No Vulkan-only implementation.
- No editor selection policy rewrite.

## Context
- `GRAPHICS-012` established CPU-visible `SelectionSystem` pick/readback/no-hit diagnostics, `EncodedSelectionId` domain/payload encoding, split selection pass class names, CPU mock command contracts for entity/face/edge/point/outline passes, and frame-recipe dependencies on surface/line/point cull buckets.
- Remaining questions affect concrete shaders, GPU readback, runtime ECS selection resolution, and editor policy and should not be mixed with pass-contract work.

## Required changes
- [x] Document exact shader-side encoding for `EntityId` and `PrimitiveId`, including authoritative face/edge/point payload sources.
- [x] Clarify the backend readback mechanism from `Picking.Readback` into `SelectionSystem::PublishPickResult()` / `PublishNoHit()`.
- [x] Clarify runtime ownership for resolving `StableEntityId` into ECS selection state and selection-outline inputs.
- [x] Clarify how transparent/special forward materials affect picking eligibility once those lanes exist.

## Tests
- [x] Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- [x] Update `docs/architecture/rendering-three-pass.md` and runtime/selection docs with the chosen backend/runtime handoff policy.

## Acceptance criteria
- [x] Backend and runtime work can implement real GPU picking/readback without changing the CPU/null graphics contracts.
- [x] Graphics remains free of ECS mutation and editor selection policy.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Reintroducing live ECS/editor ownership into graphics.
- Making GPU/Vulkan tests part of the default CPU-supported gate.

