# ARCH-003 — Cross-domain backlog convergence map

## Status
- done
- Completion date: 2026-05-09
- Owner: agent (claude/align-agentic-workflow-7iom3)
- Branch: `claude/align-agentic-workflow-7iom3`
- Commit reference: see branch history (`git log claude/align-agentic-workflow-7iom3 -- tasks/backlog/README.md tasks/done/ARCH-003-cross-domain-convergence-map.md`).

## Goal
- Make the cross-domain direction of the current backlog explicit so agents picking the next active task converge on a single coherent engine direction rather than diverging by category.

## Non-goals
- No code, build, or test changes.
- No new task IDs introduced under any backlog category.
- No mechanical file moves and no semantic refactors.
- No alteration of existing tasks' goals or scope; only cross-linking and grouping additions.
- No new architectural invariants beyond restating those already in `AGENTS.md`.

## Context
- Owner/layer: docs and agent workflow; affects `tasks/` and `docs/agent/` only.
- The repository now has parallel backlog categories (`architecture`, `assets`, `bugs`, `ecs`, `geometry`, `methods`, `physics`, `rendering`, `runtime`, `ui`). Each has its own structured tasks; only `assets/`, `rendering/`, and `runtime/` currently expose a category-level `README.md` with intent.
- Without a single high-level direction map, agents may pick tasks per-category and miss cross-domain dependencies, causing the engine to drift in incompatible directions across layers (for example, promoting `METHOD-001` rigid-body work without the `ARCH-001` physics layer decision, or starting `GRAPHICS-034` asset-backed mesh residency before `ASSETIO-001`/`GEOIO-002` finish their upstream work).
- `AGENTS.md` is authoritative for the engine mission and layering invariants. This task records how the *current* backlog converges to that mission so each backlog category's local DAG stays self-consistent and globally aligned.

## Required changes
- [x] Add a "Convergence themes" section to `tasks/backlog/README.md` listing current cross-domain themes, mapping each backlog category to its theme(s), and recording the cross-domain dependency anchors agents must respect.
- [x] Add minimal `README.md` entries to each backlog category that lacks one (`architecture/`, `bugs/`, `ecs/`, `geometry/`, `methods/`, `physics/`, `ui/`) so every category links back to the convergence map.
- [x] Cross-link related tasks (`METHOD-001 ↔ ARCH-001 ↔ HARDEN-064`) bidirectionally where current files only reference one direction.
- [x] Keep all additions factual about the current backlog state; do not introduce aspirational tasks or inflate scope.

## Tests
- [x] N/A; this is a docs/task-organization change. Repository structural checks below cover format/links.

## Docs
- [x] Update `tasks/backlog/README.md` with the convergence map.
- [x] Add `README.md` to each backlog category directory that lacks one.
- [x] No `AGENTS.md` change required; this slice does not introduce a new layering invariant.
- [x] No `docs/architecture/*` change required; this slice does not change subsystem architecture.

## Acceptance criteria
- [x] `tasks/backlog/README.md` contains a convergence themes section that names all current cross-domain themes and maps each category to themes.
- [x] Every backlog category directory has a `README.md` linking back to the convergence map.
- [x] Tasks already known to share cross-domain dependencies (`METHOD-001`, `ARCH-001`, `HARDEN-064`) cross-link each other.
- [x] `tools/agents/check_task_policy.py --strict` and `tools/docs/check_doc_links.py` remain green.
- [x] The active file under `tasks/active/` is retired to `tasks/done/` once changes are committed.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- No code, build, or test changes.
- No new task IDs introduced under any category.
- No mechanical file moves.
- No alteration of existing tasks' goals or scope; only cross-linking and grouping additions.
- Mixing this docs-only convergence work with semantic refactors in the same commit.
