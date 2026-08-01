---
id: RUNTIME-205
theme: F
depends_on:
  - RUNTIME-188
  - RUNTIME-201
  - RUNTIME-202
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex-RuntimeCleanup"
branch: "cleanup/runtime-139-203-205"
worktree: "/tmp/intrinsic-runtime-cleanup.ae4UkC"
claimed_at: "2026-08-01T10:01:47Z"
maturity_target: Retired
---
# RUNTIME-205 — Internalize SceneInteraction helpers

## Goal

- Remove the exported `GizmoFrameService` and `SelectionReadback` helper BMIs
  after localizing their state and behavior inside the sole production owner,
  `SceneInteractionModule`, with tests through public interaction contracts.

## Non-goals

- No gizmo, selection, picking, refinement, input-capture, history, or frame
  ordering behavior change.
- No Engine composition-helper cleanup; RUNTIME-203 owns that separate module
  owner.
- No deletion or privatization of durable lower feature contracts such as
  `GizmoInteraction`, `SelectionController`, copied interaction snapshots, or
  graphics selection packets/readbacks when they have real callers.
- No recombination of `SceneInteractionModule` with `SceneDocumentModule` or
  a new generic scene-editing service.

## Context

- Owner/layer: runtime scene interaction composition.
- Current production census: `SceneInteractionModule.cpp` is the only importer
  and owner of `GizmoFrameService` and `SelectionReadback`; direct contract
  tests instantiate both helper classes and thereby keep their BMIs public.
- RUNTIME-188 established the separate interaction module, copied snapshot,
  exact optional selection service, and world-replacement lifecycle.
  RUNTIME-201 established the common mutation/history transaction. This task
  removes only shallow helper names after preserving those contracts.
- The state is load-bearing and remains local: active gizmo drag/scratch,
  pick-sequence correlation, refined-primitive cache, interaction epoch,
  readback draining, and scene-clear/shutdown behavior.

## Slice plan

- **Slice A — owner-level coverage.** Reproduce helper lifecycle/correlation
  cases through SceneInteractionModule's public hooks, services, and copied
  snapshots.
- **Slice B — owner-local relocation.** Move gizmo frame and readback state into
  SceneInteraction implementation detail without changing phase order.
- **Slice C — surface deletion.** Remove the exported modules/CMake entries and
  direct helper tests after the public contracts pass.

## Required changes

- [ ] Re-run production/test consumer census and confirm both helpers have one
      production owner before privatization.
- [ ] Move `GizmoFrameService` input orchestration, scratch, packets, and drag
      cancellation into SceneInteraction implementation detail; keep
      `GizmoInteraction` as the durable lower behavior owner.
- [ ] Move `SelectionReadbackState` correlation, in-flight contexts,
      refinement cache, and readback drain/apply behavior into SceneInteraction
      implementation detail.
- [ ] Preserve exact frame-phase ordering, editor-capture gating, history
      requirements, world/document epoch revalidation, stale/out-of-order
      rejection, and scene-clear/shutdown resets.
- [ ] Delete both exported helper modules, CMake entries, direct imports, and
      tests that exist only to instantiate their state outside the owner.

## Tests

- [ ] SceneInteraction public contracts cover gizmo input, one undoable drag,
      capture gating, scene-clear cancellation/restoration, packet publication,
      and shutdown/reinitialize state reset.
- [ ] SceneInteraction public contracts cover pending pick issue, exact
      sequence correlation, out-of-order completion, stale world/entity /
      interaction-epoch rejection, cache bounds, and copied refined selection.
- [ ] World replacement and SceneDocument/SceneInteraction separation tests
      remain green with no retained live registry or history references.
- [ ] Structural ratchets prove `GizmoFrameService` and `SelectionReadback`
      public modules/direct imports are absent.
- [ ] Default CPU and sanitizer-supported gates required by the high-risk
      surface deletion pass.

## Docs

- [ ] Update runtime scene-interaction ownership docs and remove the two helper
      module inventory entries.
- [ ] Regenerate the module inventory and refresh task/session/retirement
      records.

## Acceptance criteria

- [ ] Gizmo and pick/readback behavior remains observable and covered through
      `SceneInteractionModule` public hooks/services/copied snapshots only.
- [ ] No production or test source imports the two helper modules, and their
      BMIs/CMake entries are absent.
- [ ] SceneDocument ownership, graphics packet/readback ownership, and app
      layering remain unchanged.
- [ ] No replacement helper service, registry, bridge, or Engine facade is
      introduced.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SceneInteraction|Gizmo|SelectionReadback|SelectionSnapshot|RuntimeEngineLayering' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Semantic input, selection, refinement, mutation, or history changes mixed
  into privatization.
- Moving scene interaction into Engine, app, graphics, or SceneDocument.
- Removing public behavior coverage with direct helper tests.
- Creating replacement public forwarding types or private test seams.

## Maturity

- Target: `Retired`; helper BMIs disappear only after SceneInteraction-level
  behavior, lifetime, and ordering parity are proven.
