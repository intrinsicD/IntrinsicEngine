# GRAPHICS-017Q — Camera/gizmo runtime clarification follow-ups

## Goal
- Clarify runtime/editor integration details that remain after the CPU/null `GRAPHICS-017` camera, pick-ray, and transform-gizmo render snapshot contracts.

## Non-goals
- No C++ behavior changes.
- No editor UI feature expansion.
- No graphics-side input polling, gizmo hit testing, or transform mutation.

## Context
- `GRAPHICS-017` established data-only graphics contracts for `CameraViewInput`, `PickPixelRequest`, `CameraViewSnapshot`, frustum planes, pick rays, and transform-gizmo render packets.
- Runtime/platform/editor layers still need ownership policy for producing these snapshots and applying interaction results.

## Required changes
- Clarify runtime camera controller ownership and how platform input is translated into camera motion without graphics dependencies.
- Clarify input-to-pick-request scheduling and whether pick requests are single-shot, queued, or coalesced per frame.
- Clarify transform-gizmo hit testing ownership, interaction state storage, and transform application/undo policy.
- Clarify how editor/runtime producers convert gizmo interaction state into data-only `TransformGizmoRenderPacket` spans.
- Clarify legacy interaction behavior that must become promoted implementation tasks before legacy retirement.

## Tests
- Documentation/checker only; no C++ tests required unless policy docs introduce checked manifests.

## Docs
- Update runtime, platform, graphics, and migration docs with selected ownership policies.

## Acceptance criteria
- Future runtime/editor implementation work can produce camera, pick, and gizmo snapshots without changing graphics ownership boundaries.
- Graphics remains free of input polling, live ECS/editor mutation, and transform application.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Moving input polling, gizmo hit testing, or transform mutation into `src/graphics`.

