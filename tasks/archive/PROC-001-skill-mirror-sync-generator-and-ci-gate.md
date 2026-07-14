# PROC-001 — Skill mirror sync generator and CI gate

## Goal
- Make the agent-skill mirror mechanically incapable of silently drifting from the canonical `docs/agent/*` sources, using the same generate-and-verify pattern as `docs/api/generated/module_inventory.md`.

## Non-goals
- No content/policy changes to any `docs/agent/*` source (that is `PROC-005` and later tasks).
- No restructuring of which skills exist or their routing tables.
- No new GitHub workflow file; reuse `ci-docs.yml`.
- No changes to `tools/agentkit/` — it is a separate scaffolding generator whose `tools/agent/` (singular) paths describe its own generated output, not this repository's tooling.

## Context
- Owner/layer: agent process infrastructure (`tools/agents/`, `.github/workflows/ci-docs.yml`, skill mirror root). No engine code.
- Confirmed live drift (2026-06-09): the mirrored `roles.md` was missing the drift-audit ownership paragraph present in `docs/agent/roles.md`; `session-onboarding.md` diverged from `docs/agent/prompt/prompt.md` by ~69 diff lines, including the skill-routing and multi-task-loop sections that existed only in the canonical prompt; nine further reference files had smaller divergences (104 insertions / 35 deletions total on repair).
- Root cause: `tools/agents/resync_skills.sh` did a plain `cp` (no link rewriting), was manual, and no CI step verified the mirror. The copy map lived only inside the shell script.
- Discovered during implementation: `.claude/skills` and `.codex/skills` are **symlinks** to `tools/agents/skills`, so there is exactly one physical mirror root and the "three roots" framing in the original task text was wrong. The relative links with five `../` levels were correct for the physical location; the actionable defects were the content drift and the absent gate. The sync tool verifies the symlinks resolve to the physical root instead of copying to three places.
- `ci-docs.yml` already implements generate-and-verify for the module inventory; this task applies the identical idiom to the skill mirror.
- Blocks `PROC-005` (contract-text fix must propagate through a trusted sync) and every later PROC task that edits mirrored docs.

## Required changes
- [x] Add `tools/agents/sync_skills.py` with the source→destination map from `resync_skills.sh`, supporting `--write` and `--check` modes against the physical root `tools/agents/skills/`.
- [x] In `--write` mode, rewrite Markdown links during copy so every link that resolves from the source location also resolves from the mirror location (relative-path recomputation; external `http(s)`/`mailto` links and intra-file anchors untouched; targets that do not exist in the repo left unchanged).
- [x] In `--check` mode, regenerate in memory, diff against the mirror, print per-file mismatches with a "run: python3 tools/agents/sync_skills.py --write" remediation line, and exit nonzero on any mismatch or missing mapped source.
- [x] Verify the `.claude/skills` and `.codex/skills` surfaces resolve to the physical root (replaces the original cross-root byte-identity item, which assumed three physical copies).
- [x] Run `--write` once to repair the current drift (11 files).
- [x] Replace `tools/agents/resync_skills.sh` with a thin wrapper that calls `sync_skills.py --write`, and update `tools/agents/README.md` (the `tools/agentkit/README.md` references turned out to describe agentkit's own generated scaffold and were correctly left alone).
- [x] Add a `Validate agent skill mirrors` step to `.github/workflows/ci-docs.yml` running `python3 tools/agents/sync_skills.py --check`.

## Tests
- [x] `python3 tools/agents/sync_skills.py --check` exits 0 on the repaired tree and is idempotent after `--write`.
- [x] Mutate one mirror file locally, confirm `--check` exits nonzero and names the file, then restore via `--write`.
- [x] `python3 tools/docs/check_doc_links.py --root .` passes after the link-rewriting `--write` run.
- [x] `python3 tools/agents/check_task_policy.py --root . --strict` passes.

## Docs
- [x] Update `tools/agents/README.md` with the `sync_skills.py` modes, the symlink-surface model, and the CI gate.
- [x] Update the `AGENTS.md` "Agent skills" section with how mirrors stay synchronized (edit canonical docs, re-run sync, never edit mirror copies).

## Acceptance criteria
- [x] The physical mirror root is byte-identical to the generated output, and the `.claude`/`.codex` symlinks resolve to it.
- [x] The previously missing `roles.md` drift-audit paragraph and the prompt's skill-routing/multi-task-loop sections are present in the mirror.
- [x] `ci-docs.yml` fails when the mirror diverges from `docs/agent/*`.
- [x] Editing a canonical doc without re-running sync is caught by CI, not by a human.

## Verification
```bash
python3 tools/agents/sync_skills.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Changing the meaning of any mirrored document while repairing the sync (content fixes belong to `PROC-005` or follow-ups).
- Deleting the mirror root or the `.claude`/`.codex` symlink surfaces.

## Completion

- Completed 2026-06-09 on branch `claude/agentic-workflow-analysis-kohifk`.
- Deviation from original plan: the task was written assuming three physical
  mirror roots; `.claude/skills` and `.codex/skills` are symlinks to
  `tools/agents/skills`, so the tool syncs one physical root and verifies the
  symlink surfaces instead. `tools/agentkit/` was left untouched (separate
  generator, not this repo's tooling).
- Commit: the PROC-001 implementation commit on branch `claude/agentic-workflow-analysis-kohifk` (sync tool, drift repair, CI gate, wrapper, docs).
