# GRAPHICS-012Q — Picking backend/runtime clarification follow-ups

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
- Document exact shader-side encoding for `EntityId` and `PrimitiveId`, including authoritative face/edge/point payload sources.
- Clarify the backend readback mechanism from `Picking.Readback` into `SelectionSystem::PublishPickResult()` / `PublishNoHit()`.
- Clarify runtime ownership for resolving `StableEntityId` into ECS selection state and selection-outline inputs.
- Clarify how transparent/special forward materials affect picking eligibility once those lanes exist.

## Tests
- Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- Update `docs/architecture/rendering-three-pass.md` and runtime/selection docs with the chosen backend/runtime handoff policy.

## Acceptance criteria
- Backend and runtime work can implement real GPU picking/readback without changing the CPU/null graphics contracts.
- Graphics remains free of ECS mutation and editor selection policy.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Reintroducing live ECS/editor ownership into graphics.
- Making GPU/Vulkan tests part of the default CPU-supported gate.

