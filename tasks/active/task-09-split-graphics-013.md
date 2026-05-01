# Task 9 — Split GRAPHICS-013 into smaller tasks

- Status: planned (queued for Codex)
- Owner: TBD
- Branch / PR: TBD
- Next verification step: `python3 tools/agents/check_task_policy.py --root . --strict` after creating GRAPHICS-013A/B/C.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Split the over-scoped GRAPHICS-013 postprocess/debugview/imgui/present task into smaller scoped tasks.

## Current problem

GRAPHICS-013 bundles bloom, FXAA, SMAA, tone-map, histogram, debug view, ImGui overlay, and present. This is too large for the agent workflow.

## Required changes

1. Keep GRAPHICS-013 as an umbrella/index task OR retire it into done as superseded planning.

   Preferred:
   - Keep GRAPHICS-013 as umbrella planning task with no implementation.
   - Add links to split tasks.

2. Create:
   - `tasks/backlog/rendering/GRAPHICS-013A-postprocess-chain.md`
   - `tasks/backlog/rendering/GRAPHICS-013B-debug-view-and-render-target-inspection.md`
   - `tasks/backlog/rendering/GRAPHICS-013C-imgui-overlay-and-present.md`

3. GRAPHICS-013A should own:
   - bloom
   - FXAA
   - SMAA
   - tone map
   - histogram
   - HDR to LDR chain
   - postprocess resource lifetime

4. GRAPHICS-013B should own:
   - debug-view sampled-resource selection
   - render-target inspection hooks
   - debug preview output
   - diagnostics for missing sampled resources

5. GRAPHICS-013C should own:
   - ImGui draw-data import
   - overlay load/store behavior
   - final present/finalization pass
   - imported backbuffer write policy

6. Update GRAPHICS-001 task index:
   - Replace GRAPHICS-013 with GRAPHICS-013A/B/C in intended order.
   - Keep dependencies clear.

7. Update `tasks/backlog/rendering/README.md` dependency DAG.

## Acceptance criteria

- No single split task owns unrelated feature families.
- Each split task has complete required sections.
- GRAPHICS-001 and rendering README point to the split tasks.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes

- No renderer implementation.
- No shader implementation.
- No pass code changes.

This keeps Codex patches small and scoped, matching the agent contract.
