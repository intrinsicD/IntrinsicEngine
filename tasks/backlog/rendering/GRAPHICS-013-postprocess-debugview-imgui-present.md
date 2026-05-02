# GRAPHICS-013 — Post-process, debug view, ImGui, and present (umbrella)
## Goal
- Keep a high-level planning index for the formerly over-scoped rendering follow-up area.
- Delegate implementation planning to split tasks with narrow ownership boundaries.
## Non-goals
- No implementation details in this umbrella task.
- No renderer, shader, or pass code changes.
## Context
- This task was originally over-scoped and bundled multiple independent feature families.
- Split ownership now lives in GRAPHICS-013A/B/C for reviewability and execution sequencing.
## Required changes
- Use this file as an index only.
- Plan and execute implementation through:
  - `GRAPHICS-013A` postprocess chain.
  - `GRAPHICS-013B` debug-view and render-target inspection.
  - `GRAPHICS-013C` ImGui overlay and present/finalization.
## Tests
- N/A in this umbrella file; tests are defined by split tasks.
## Docs
- Keep cross-links synchronized with GRAPHICS-001 and rendering README DAG/order.
## Acceptance criteria
- GRAPHICS-013A/B/C collectively replace implementation scope previously attached to GRAPHICS-013.
- Each split task owns one coherent feature family with explicit boundaries.
## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```
## Forbidden changes
- Re-expanding this umbrella into implementation detail.
- Mixing unrelated feature families back into one task.
