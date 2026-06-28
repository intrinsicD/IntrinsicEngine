---
id: GRAPHICS-107
theme: B
depends_on: [GRAPHICS-106]
maturity_target: CPUContracted
---
# GRAPHICS-107 — Reconcile the FrameRecipe vs RenderRecipe vocabularies

## Goal
- Make it unambiguous which type drives the live frame and which is the
  config/contract overlay, so a reader can identify the single authoritative
  frame-composition path (P5 readability; removes the structural reason the
  config lane reads first-class but cannot drive a frame).

## Non-goals
- Renaming/merging the types in a behavior-changing way (Slice A is doc/locality
  only; the live `FrameRecipe*` path is not touched behaviorally).
- Widening contract vocabulary (`GRAPHICS-099`) or injecting pass-graph nodes
  (`GRAPHICS-101` fixed core).

## Context
- Two parallel vocabularies coexist: the live `FrameRecipeFeatures` / `FrameRecipe*`
  frame driver (`src/graphics/renderer/Graphics.FrameRecipe.cppm`,
  `BuildDefaultFrameRecipe`) and the config/contract `RenderRecipeDescriptor` /
  `RenderRecipeConfig` overlay. The split is a readability/abstraction smell and
  the reason the config lane cannot yet drive a frame.
- This is the enabling-refactor framing of the same root cause as `GRAPHICS-106`;
  kept separate and medium so the seam ships first and this reuses its pure
  projection.
- Current state: Slice A is implemented. `src/graphics/renderer/README.md` and
  `docs/architecture/frame-graph.md` now name `FrameRecipe*` as the live
  per-frame driver, `RenderRecipe*` as the contract/config overlay, and
  `FrameRecipeOverride` / `ProjectFrameRecipeOverride(...)` as the
  `GRAPHICS-106` seam between them. Slice B remains open for projection
  behavior/tests.
- Owner/layer: `graphics` (renderer).

## Required changes
- [x] Slice A (readability, no behavior change): add header/doc notes and
      cross-references so a reader can tell `FrameRecipe*` is the live frame
      driver and `RenderRecipe*` is the config/contract overlay; mark the seam
      where the overlay applies (the `GRAPHICS-106` projection).
- [ ] Slice B: reuse the pure projection from `GRAPHICS-106`; constrain mapping to
      optional-slot/feature flags only — no arbitrary pass-graph injection, no new
      contract vocabulary.

## Tests
- [ ] Unit test the projection function in isolation (shared with `GRAPHICS-106`).
- [x] Slice A is behavior-preserving: docs/comment-only; docs/task checks pass.
- [ ] Default CPU gate stays green.

## Docs
- [x] Update `src/graphics/renderer/README.md` (and `docs/architecture/frame-graph.md`
      after `DOCS-004`) to name the single authoritative frame-composition path.

## Acceptance criteria
- [x] A renderer reader can identify the single authoritative frame-composition
      path and where the config overlay applies.
- [ ] The projection function is unit-tested in isolation.
- [x] Slice A introduces no behavior change.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Behavior-changing renames in Slice A.
- Widening `RenderRecipeDescriptor` vocabulary or injecting pass-graph nodes.
- Mixing the mechanical doc/locality slice with the semantic projection slice.

## Maturity
- Target: `CPUContracted`. Slice A is doc/locality only; Slice B's projection is
  contract-tested. No `Operational` follow-up is owed beyond `RUNTIME-130`'s
  live-frame wiring.
- Slice A landed as docs/locality only; task remains active until Slice B closes
  the projection-test gate.
