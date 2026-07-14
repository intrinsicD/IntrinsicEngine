# HARDEN-069 — Rebind legacy layering allowlist entries to active retirement tasks

## Goal
- Re-anchor every entry in `tools/repo/layering_allowlist.yaml` (currently 81 entries, ~1339 allowlisted edges) on a removal task that is actually open in `tasks/active/` or `tasks/backlog/`, rather than on the already-closed `HARDEN-010` ID.

## Non-goals
- Do not delete or migrate any code under `src/legacy/` in this task. Allowlist rebinding only.
- Do not relax any layering rule. The set of allowlisted edges must remain identical (same `from`, `to`, `file_glob`) before and after this change.
- Do not author the per-subtree LEGACY-* retirement tasks here. That work belongs to [`LEGACY-002`](LEGACY-002-seed-src-legacy-retirement-backlog.md) and any per-subtree successors.
- Do not change the layering checker scripts.

## Context
- Owning subsystem/layer: repository tooling under `tools/repo/` and the architecture/migration docs that describe legacy retirement.
- Source of the violation: `tools/repo/layering_allowlist.yaml` references `task: "HARDEN-010"` on every row, but [`HARDEN-010`](HARDEN-010-layering-allowlist-path-specific-exceptions.md) is in `tasks/done/`. Per [`AGENTS.md`](../../AGENTS.md) §13, every temporary exception must point at a current task and have a specific removal task ID.
- Allowlist edges currently cover 9 destination layers (`core`, `geometry`, `assets`, `ecs`, `graphics_rhi`, `graphics`, `runtime`, `platform`, `app`) × 9 subtree globs (`src/legacy/{Apps,Asset,Core,ECS,EditorUI,Graphics,Interface,RHI,Runtime}/**`).
- Allowlist-quality checker: [`tools/repo/check_layering_allowlist_quality.py`](../../tools/repo/check_layering_allowlist_quality.py). It must keep passing after this change.

## Required changes
- [x] For each row in `tools/repo/layering_allowlist.yaml`, replace the `task:` and `expires:` fields so the task ID is the open retirement task that owns the source subtree of that row:
  - Rows whose `file_glob` matches `src/legacy/Interface/**` -> `task: "LEGACY-001"`, `expires: "until LEGACY-001 retires src/legacy/Interface/"`.
  - Rows whose `file_glob` matches the remaining 8 subtrees -> point at the per-subtree task seeded by `LEGACY-002` (or, if `LEGACY-002` has not yet landed, point at `LEGACY-002` itself with a transitional expiry note).
- [x] Verify the entry count stays at 81 and the set of `(from, to, file_glob)` triples is unchanged (this is a metadata-only edit).
- [x] Add a one-line preamble comment at the top of `tools/repo/layering_allowlist.yaml` describing the rule: every row must point at an open removal task.
- [x] Update [`tools/repo/layering_allowlist.yaml`](../../tools/repo/layering_allowlist.yaml) docstring or sibling `README.md` if one exists, to record the new rebinding rule.

## Tests
- [x] `python3 tools/repo/check_layering.py --root src --strict` keeps passing with the same allowlisted-violation count as before the change.
- [x] `python3 tools/repo/check_layering_allowlist_quality.py --root .` passes.
- [x] `python3 tools/agents/check_task_policy.py --root . --strict` passes.
- [x] `python3 tools/agents/validate_tasks.py --root tasks --strict` passes.

## Docs
- [x] Update [`docs/migration/legacy-retirement.md`](../../docs/migration/legacy-retirement.md) (or the closest current legacy-retirement note) to record that the allowlist now references open retirement tasks per subtree.
- [x] Cross-link this task and `LEGACY-002` from the hardening tracker note in [`tasks/archive/0001-post-reorganization-hardening-tracker.md`](0001-post-reorganization-hardening-tracker.md) status table (append-only follow-up row).

## Acceptance criteria
- [x] No row in `tools/repo/layering_allowlist.yaml` references a task ID that lives under `tasks/done/`.
- [x] Every row's `expires:` field names a concrete retirement task or subtree, not "path-specific legacy retirement tasks" in the generic.
- [x] Allowlisted-edge count reported by `check_layering.py` is unchanged.
- [x] `check_layering_allowlist_quality.py` reports `No findings.`
- [x] Diff for this commit is restricted to `tools/repo/layering_allowlist.yaml`, its sibling docs, the legacy-retirement migration doc, and the hardening tracker.

## Verification
```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_layering_allowlist_quality.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .

# Sanity: confirm no allowlist row still points at HARDEN-010 (a done task).
! grep -E '^\s+task:\s*"HARDEN-010"' tools/repo/layering_allowlist.yaml
```

## Forbidden changes
- Mixing this metadata rebinding with any deletion under `src/legacy/`.
- Adding, removing, or widening any `(from, to, file_glob)` allowlist edge.
- Pointing rows at task IDs that do not exist or that live in `tasks/done/`.
- Disabling or weakening `check_layering_allowlist_quality.py`.


## Completion
- Completed: 2026-06-02.
- Status: done.
- Commit reference: this commit (`HARDEN-069: rebind legacy allowlist owners`).
