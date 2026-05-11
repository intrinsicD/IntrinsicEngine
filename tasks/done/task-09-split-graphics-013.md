# Task 9 — Split GRAPHICS-013 into smaller tasks

- Status: completed (2026-05-02)
- Owner: Codex (gpt-5.3-codex)
- Branch / PR: current branch / TBD
- Completion date: 2026-05-02
- Commit / PR: local split branch `split/current-working-tree-2026-05-02`; remote PR reference TBD.
- Follow-ups: implementation remains in GRAPHICS-013A/B/C backlog tasks.
- Next verification step: `python3 tools/agents/check_task_policy.py --root . --strict` after creating GRAPHICS-013A/B/C.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Split the over-scoped GRAPHICS-013 postprocess/debugview/imgui/present task into smaller scoped tasks.

## Current problem

GRAPHICS-013 bundles bloom, FXAA, SMAA, tone-map, histogram, debug view, ImGui overlay, and present. This is too large for the agent workflow.

## Required changes

- [x] Keep GRAPHICS-013 as an umbrella/index task OR retire it into done as superseded planning.

   Preferred:
   - [x] Keep GRAPHICS-013 as umbrella planning task with no implementation.
   - [x] Add links to split tasks.

- [x] Create:
   - [x] `tasks/backlog/rendering/GRAPHICS-013A-postprocess-chain.md`
   - [x] `tasks/backlog/rendering/GRAPHICS-013B-debug-view-and-render-target-inspection.md`
   - [x] `tasks/backlog/rendering/GRAPHICS-013C-imgui-overlay-and-present.md`

- [x] GRAPHICS-013A should own:
   - [x] bloom
   - [x] FXAA
   - [x] SMAA
   - [x] tone map
   - [x] histogram
   - [x] HDR to LDR chain
   - [x] postprocess resource lifetime

- [x] GRAPHICS-013B should own:
   - [x] debug-view sampled-resource selection
   - [x] render-target inspection hooks
   - [x] debug preview output
   - [x] diagnostics for missing sampled resources

- [x] GRAPHICS-013C should own:
   - [x] ImGui draw-data import
   - [x] overlay load/store behavior
   - [x] final present/finalization pass
   - [x] imported backbuffer write policy

- [x] Update GRAPHICS-001 task index:
   - [x] Replace GRAPHICS-013 with GRAPHICS-013A/B/C in intended order.
   - [x] Keep dependencies clear.

- [x] Update `tasks/backlog/rendering/README.md` dependency DAG.

## Acceptance criteria

- [x] No single split task owns unrelated feature families.
- [x] Each split task has complete required sections.
- [x] GRAPHICS-001 and rendering README point to the split tasks.

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
