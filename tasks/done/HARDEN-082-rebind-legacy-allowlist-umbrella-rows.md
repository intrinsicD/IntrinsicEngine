---
id: HARDEN-082
theme: F
depends_on: []
---
# HARDEN-082 — Rebind legacy allowlist umbrella rows to per-subtree owners

## Goal

- Move every `tools/repo/layering_allowlist.yaml` row still owned by the
  `LEGACY-002` seeding umbrella to its specific per-subtree retirement task
  (`LEGACY-004..006`, `LEGACY-008..010`), so each temporary exception names
  the concrete task that deletes it and `LEGACY-002` can retire.

## Non-goals

- No `src/legacy/` code changes.
- No allowlist rows added or removed; this is a metadata-only owner rebind.
- No changes to `check_layering.py` semantics.

## Context

- Owner/layer: layering governance metadata (`tools/repo/layering_allowlist.yaml`).
- `LEGACY-002` seeded the per-subtree retirement tasks but recorded (Context,
  2026-06-06) that ~54 allowlist rows still name `LEGACY-002` as their open
  umbrella owner, and that a metadata-only rebinding follow-up mirroring
  [`HARDEN-069`](HARDEN-069-rebind-legacy-layering-allowlist-to-active-retirement-tasks.md)
  must move each subtree's rows to its specific `LEGACY-00N` task before
  `LEGACY-002` can retire. This is that follow-up.
- Mapping by `file_glob` prefix: `src/legacy/Asset/**` → `LEGACY-004`,
  `src/legacy/Core/**` → `LEGACY-005`, `src/legacy/ECS/**` → `LEGACY-006`,
  `src/legacy/Graphics/**` → `LEGACY-008`, `src/legacy/RHI/**` → `LEGACY-009`,
  `src/legacy/Runtime/**` → `LEGACY-010`. `src/legacy/Interface/**` rows
  already name `LEGACY-001` and are untouched.
- The rows' `expires` text ("until LEGACY-002 seeds the ... retirement task")
  is already satisfied; it is rewritten to "until LEGACY-00N deletes ..." so
  every exception stays time-bounded against its real removal owner.
- Executed (2026-06-10): all 54 `LEGACY-002` rows rebound (9 per subtree across Asset/Core/ECS/Graphics/RHI/Runtime), expires text rewritten per owner, no row count or glob changes (108 changed lines = 54 task + 54 expires strings), strict layering check green with the allowlisted-violation count unchanged at 1187, and `grep -c 'task: "LEGACY-002"'` returns 0.
- Completed: 2026-06-10.
- PR/commit: branch `claude/pensive-albattani-pu2t14` (pending local commit).

## Required changes

- [x] Rebind each `task: "LEGACY-002"` row to its per-subtree owner by
      `file_glob` prefix and update the row's `expires` text accordingly.
- [x] Confirm no row count change and no glob change
      (`git diff --stat` touches only owner/expiry strings).

## Tests

- [x] `python3 tools/repo/check_layering.py --root src --strict` passes with
      the same allowlisted-violation count as before the rebind.
- [x] `grep -c 'task: "LEGACY-002"' tools/repo/layering_allowlist.yaml`
      returns 0.

## Docs

- [x] None beyond the task records; the allowlist is self-describing.

## Acceptance criteria

- [x] Every allowlist row names an open per-subtree retirement task
      (`LEGACY-001`, `LEGACY-004..006`, `LEGACY-008..010`).
- [x] `LEGACY-002` is unblocked for retirement (no allowlist row references
      it).
- [x] Strict layering check green with unchanged violation count.

## Verification

```bash
grep -c 'task: "LEGACY-002"' tools/repo/layering_allowlist.yaml   # expect 0
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Mixing mechanical file moves with semantic refactors.
- Adding or removing allowlist rows or changing any `file_glob`.
- Touching `src/legacy/` or `check_layering.py`.

## Maturity

- Target: `Retired` (metadata-only governance rebind; no engine seam, no
  `Operational` follow-up owed).
