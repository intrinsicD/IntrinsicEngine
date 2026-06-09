# PROC-001 — Skill mirror sync generator and CI gate

## Goal
- Make the agent-skill mirrors under `tools/agents/skills/`, `.claude/skills/`, and `.codex/skills/` mechanically incapable of silently drifting from the canonical `docs/agent/*` sources, using the same generate-and-verify pattern as `docs/api/generated/module_inventory.md`.

## Non-goals
- No content/policy changes to any `docs/agent/*` source (that is `PROC-005` and later tasks).
- No restructuring of which skills exist or their routing tables.
- No symlink scheme — mirror consumers (skill auto-discovery, non-POSIX checkouts) need real files.
- No new GitHub workflow file; reuse `ci-docs.yml`.

## Context
- Owner/layer: agent process infrastructure (`tools/agents/`, `.github/workflows/ci-docs.yml`, skill mirror roots). No engine code.
- Confirmed live drift (2026-06-09): the mirrored `roles.md` is missing the drift-audit ownership paragraph present in `docs/agent/roles.md`; the mirrored copy carries a mangled relative link (`docs/agent/../../../../../docs/agent/...`); `session-onboarding.md` diverges from `docs/agent/prompt/prompt.md` by ~69 diff lines, including the skill-routing and multi-task-loop sections that exist only in the canonical prompt.
- Root cause: `tools/agents/resync_skills.sh` does a plain `cp` (breaking links that are relative to `docs/agent/`), is manual, and no CI step verifies the mirrors. The copy map lives only inside the shell script.
- `ci-docs.yml` already implements generate-and-verify for the module inventory (`generate_module_inventory.py` + `git diff --exit-code`); this task applies the identical idiom to skill mirrors.
- Blocks `PROC-005` (contract-text fix must propagate through a trusted sync) and every later PROC task that edits mirrored docs.

## Required changes
- [ ] Add `tools/agents/sync_skills.py` with the source→destination map from `resync_skills.sh` plus all three mirror roots (`tools/agents/skills/`, `.claude/skills/`, `.codex/skills/`), supporting `--write` and `--check` modes.
- [ ] In `--write` mode, rewrite Markdown links during copy so every link that resolves from the source location also resolves from each mirror location (compute the relative path from the destination file to the original link target; leave external `http(s)` links and intra-file anchors untouched).
- [ ] In `--check` mode, regenerate into a temporary directory, diff against all three mirror roots, print per-file mismatches with a "run: python3 tools/agents/sync_skills.py --write" remediation line, and exit nonzero on any mismatch (also nonzero when a mapped source file is missing).
- [ ] Cover skill-only files (`SKILL.md` bodies, skill `README.md`s) by checking that the three mirror roots are byte-identical to each other even where no `docs/agent/` source exists.
- [ ] Run `--write` once to repair the current drift across all three roots.
- [ ] Replace `tools/agents/resync_skills.sh` with a two-line wrapper that calls `sync_skills.py --write`, and update the references in `tools/agents/README.md` and `tools/agentkit/README.md` (which currently cites a wrong path `tools/agent/resync_skills.sh`).
- [ ] Add a `Validate agent skill mirrors` step to `.github/workflows/ci-docs.yml` running `python3 tools/agents/sync_skills.py --check`.

## Tests
- [ ] `python3 tools/agents/sync_skills.py --check` exits 0 on the repaired tree.
- [ ] Mutate one mirror file locally, confirm `--check` exits nonzero and names the file, then restore via `--write`.
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes after the link-rewriting `--write` run (this is the regression test for the mangled-link failure mode).
- [ ] `python3 tools/agents/check_task_policy.py --root . --strict` passes.

## Docs
- [ ] Update `tools/agents/README.md` with the `sync_skills.py` modes and the CI gate.
- [ ] Update the `AGENTS.md` "Agent skills" section sentence describing how mirrors stay synchronized (one sentence; no policy change).

## Acceptance criteria
- [ ] All three mirror roots are byte-identical to the generated output and to each other.
- [ ] The previously missing `roles.md` drift-audit paragraph and the prompt's skill-routing/multi-task-loop sections are present in all mirrors.
- [ ] No mirror file contains a `../../../../..`-style mangled link.
- [ ] `ci-docs.yml` fails when any mirror diverges from `docs/agent/*`.
- [ ] Editing a canonical doc without re-running sync is caught by CI, not by a human.

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
- Deleting any of the three mirror roots or replacing them with symlinks.
