---
id: BUG-089
theme: G
depends_on: []
---
# BUG-089 — Root-hygiene strict mode rejects canonical and ignored state

## Status

- Completed on 2026-07-16 at `CPUContracted` maturity. Implementation commit:
  `7671576b`. One repository-owned policy now distinguishes exact tracked
  roots from bounded named local state, and both checker entrypoints use it.

## Goal

- Restore a meaningful strict root-hygiene gate that accepts the repository's
  intentional tracked roots and ignores known local/VCS-ignored state while
  continuing to reject genuinely unowned top-level entries.

## Non-goals

- No blanket warning-mode conversion, wildcard allowance for arbitrary root
  files, or deletion of research records.
- No change to the policy limiting root-level Markdown to `README.md`,
  `AGENTS.md`, and `CLAUDE.md`.
- No cleanup of unrelated build outputs or user files beyond the checker and
  its canonical policy data/tests.

## Context

- Claimed on 2026-07-16 by Codex on branch
  `codex/arch-006-completion`.

- Reproduction on 2026-07-15:
  `python3 tools/repo/check_root_hygiene.py --root . --strict` exits 1 and
  reports `.ruff_cache/`, tracked `ara/`, and ignored `imgui.ini` as unexpected.
- `ara/` is a checked-in Agent-Native Research Artifact root. `imgui.ini` is
  explicitly documented in `tools/repo/root_allowlist.yaml` as ignored local
  editor state, and `.ruff_cache/` is disposable local tool state.
- The checker currently applies only a hard-coded local-pattern set and does
  not reconcile the policy note or VCS ignore status, so a working tree can
  fail strict hygiene without any source-layout violation.
- This blocks the commit-scoped readiness audit in `REVIEW-003`; the audit must
  not weaken or skip strict root hygiene to proceed.

## Right-sizing

- Element: root-entry policy is duplicated between two CLI checkers, while
  one documented YAML file is already named as their shared authority.
- Simpler alternative: one canonical checker with a plain policy record and
  parsing/matching free functions; the older top-level checker path becomes a
  thin compatibility entrypoint and is no longer run redundantly. No registry,
  service, plugin, or Git integration.
- Blast radius: the two checker CLIs, one YAML policy, focused Python
  regressions, and repository-hygiene documentation only.
- Reintroduction trigger: none; a second policy backend would require a
  separately reviewed repository-policy task.

## Required changes

- [x] Decide and document the canonical status of tracked `ara/`; if it remains
      a repository root, add it explicitly to the allowlist rather than using a
      broad pattern.
- [x] Make the checker ignore only named disposable local roots and/or entries
      proven ignored by repository policy, without allowing an untracked source
      directory merely because a developer's global Git configuration ignores
      it.
- [x] Reconcile `imgui.ini` and `.ruff_cache/` handling with the canonical
      allowlist/ignore policy so the documented notes and executable check agree.
- [x] Keep diagnostics distinguishing allowed tracked roots, ignored local
      state, unexpected entries, and missing required entries.

## Tests

- [x] Add isolated filesystem fixtures proving strict mode accepts the canonical
      root set, tracked `ara/`, ignored `imgui.ini`, and disposable
      `.ruff_cache/`.
- [x] Prove strict mode still rejects an unowned root Markdown file, an unknown
      source directory, and a missing required root.
- [x] Prove a global/user ignore rule alone cannot hide an unexpected source
      root from the repository check.

## Docs

- [x] Update `tools/repo/root_allowlist.yaml` notes and the owning repository-
      hygiene documentation with the exact tracked/local-state policy.
- [x] Update this bug index and retirement log after the strict command passes
      in both a clean checkout and a representative developer worktree.

## Acceptance criteria

- [x] `python3 tools/repo/check_root_hygiene.py --root . --strict` passes with
      the intentional tracked roots and named ignored local state present.
- [x] The same command fails closed for genuinely unexpected top-level source
      or Markdown entries.
- [x] The allowlist and executable ignore behavior express the same policy; no
      blanket wildcard or warning-only escape is introduced.
- [x] Task, docs-link, and repository structural checks remain green.

## Verification

```bash
python3 tools/repo/check_root_hygiene.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes

- Ignoring every VCS-ignored top-level path without repository-owned policy.
- Adding `*`, arbitrary developer directories, or generated outputs to the
  root allowlist.
- Deleting or relocating `ara/` without a separately reviewed research-artifact
  migration.
- Weakening strict mode or suppressing unexpected-entry diagnostics.
