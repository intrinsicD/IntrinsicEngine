# GEOM-004 — `src/geometry` gap analysis

## Goal
- Produce a repository-grounded gap analysis for `src/geometry` covering style inconsistencies, missing reusable data structures, and missing general algorithms relevant to modern geometry-processing papers.

## Non-goals
- No C++ behavior changes.
- No module renames or mechanical moves.
- No new geometry algorithms or benchmark implementations.

## Context
- Owning subsystem/layer: `geometry`.
- Status: done.
- Owner/agent: GitHub Copilot.
- Created: 2026-05-12.
- Completed: 2026-05-12.
- Commit: pending in this change.
- `geometry` may depend on `core` and must not depend on runtime, graphics, ECS, assets, platform, or app ownership.
- This is a documentation-only review intended to seed follow-up backlog tasks.

## Required changes
- [x] Inspect current `src/geometry` modules and representative implementations.
- [x] Inspect geometry tests and benchmark placeholders.
- [x] Identify style/API inconsistencies.
- [x] Identify missing reusable data structures and general algorithms for paper-method work.
- [x] Record the recommended GLM + Eigen3 linear algebra policy.
- [x] Record prioritized follow-up recommendations.

## Tests
- [x] Run documentation/task structural checks relevant to the touched files.

## Docs
- [x] Add `docs/reviews/2026-05-12-src-geometry-gap-analysis.md`.

## Acceptance criteria
- [x] Gap analysis is factual and references current repository state.
- [x] No source behavior is changed.
- [x] Follow-up work is split into suggested task-sized items.
- [x] Verification commands and results are recorded in this task.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Results on 2026-05-12:

- Documentation link validation passed: 321 relative links checked; no broken relative links found.
- Task policy validation passed: 194 task files checked; findings=0 in strict mode.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding new dependencies or layer edges.




