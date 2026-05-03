# GRAPHICS-008Q — Surface pass clarification follow-ups

## Goal
- Resolve remaining policy questions discovered while completing the CPU/null depth, forward surface, and G-buffer pass contracts.

## Non-goals
- No implementation changes.
- No Vulkan-only work.

## Context
- Owner: `src/graphics/renderer/Passes`, `src/graphics/renderer/Graphics.FrameRecipe`, and rendering architecture docs.
- Created during `GRAPHICS-008` completion as the backlog location for questions that should not block the CPU-testable pass command contracts.

## Required changes
- Decide whether alpha-mask surfaces need a dedicated depth prepass/G-buffer pass bucket before material alpha evaluation is implemented.
- Decide when to introduce an explicit descriptor-bind command seam versus continuing to use `GpuScenePushConstants::SceneTableBDA` for scene-table access.
- Define renderpass attachment ownership details for backend implementations: depth load/store state when depth prepass is enabled, G-buffer MRT clear/load policy, and forward path depth-write state.
- Decide whether invalid or empty culling buckets should emit per-pass diagnostics or remain silent no-op command skips.

## Tests
- Add/update CPU contract tests only when a policy decision becomes implementation work.

## Docs
- Update `docs/architecture/rendering-three-pass.md` if pass resource ownership or binding policy changes.

## Acceptance criteria
- Open questions above have explicit decisions or child implementation tasks.
- Downstream lighting/shadow work can reference the chosen surface/depth/G-buffer policy without reopening `GRAPHICS-008`.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- No code changes in this clarification task.
- No legacy pass dependency expansion.

