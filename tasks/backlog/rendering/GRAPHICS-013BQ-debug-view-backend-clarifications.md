# GRAPHICS-013BQ — Debug-view backend clarification follow-ups

## Goal
- Clarify concrete backend and UX details that remain after the CPU/null `GRAPHICS-013B` debug-view and render-target inspection contracts.

## Non-goals
- No C++ behavior changes.
- No postprocess effect ownership.
- No ImGui overlay or present/finalization policy work.

## Context
- `GRAPHICS-013B` established `DebugViewSystem` resource inspection, deterministic resource selection/fallback diagnostics, `DebugViewPushConstants`, `Pass.DebugView` fullscreen command contracts, and frame-recipe `DebugViewRGBA` scheduling.
- Remaining questions affect shader-side visualization modes, descriptor binding, UI selection plumbing, and backend formatting and should not be mixed with CPU/null contracts.

## Required changes
- Clarify shader visualization modes for color, depth, integer ID, normal, material, and shadow atlas resources.
- Clarify descriptor binding ownership for the selected sampled resource and `DebugViewRGBA` output.
- Clarify how runtime/editor UI names map to `FrameRecipeIntrospection` resource names without adding platform/window ownership to graphics.
- Clarify whether unsupported buffer resources should later expose textual/statistical inspection outside the fullscreen preview path.

## Tests
- Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- Update `docs/architecture/rendering-three-pass.md`, renderer docs, and backend notes with selected debug-view backend policies.

## Acceptance criteria
- Backend/UI debug-view work can proceed without changing the CPU/null graphics contracts from `GRAPHICS-013B`.
- Graphics remains decoupled from platform/window ownership and ImGui policy.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Expanding into ImGui/present ownership.
- Making GPU/Vulkan tests part of the default CPU-supported gate.

