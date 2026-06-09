# PROC-005 — Align structural-check mode text with strict CI reality

## Goal
- Correct the stale "warning mode" language in the agent contract so it matches CI reality (structural checks already run `--strict` in `ci-docs.yml`), and define the tracked-exception rule for any future check that genuinely starts in warning mode.

## Non-goals
- No changes to which checks run or their modes in any workflow — this is a docs-only truth-up.
- No new ratchet/baseline tooling; the existing strict gates already provide enforcement.

## Context
- Owner/layer: contract docs (`AGENTS.md`, `docs/agent/contract.md`) and their skill mirrors. No engine code, no CI behavior change.
- Stale claims (2026-06-09): `AGENTS.md` §10 says "Structural checks (tasks/docs/layering/manifests) should run in warning mode until explicitly tightened" and `docs/agent/contract.md:90` says "Structural checks can start in warning mode and later tighten" — but `ci-docs.yml` already runs doc links, task policy, codex config, layering-allowlist quality, test layout, method manifests, and benchmark manifests with `--strict`.
- This violates the contract's own docs-sync rule ("Keep docs factual (current state), not aspirational"). Warning-mode language also trains agents to discount check output.
- Depends on `PROC-001` so the corrected text propagates to mirrors through a verified sync rather than a manual copy.

## Required changes
- [ ] Rewrite `AGENTS.md` §10's structural-check bullet to state current reality: the structural checks listed run strict in `ci-docs.yml`; name the strict set or point to the workflow file.
- [ ] Add the tracked-exception rule in the same bullet: a newly introduced check may run in warning mode only while a referenced task ID owns its tightening, mirroring the §13 temporary-exception idiom.
- [ ] Apply the matching correction to `docs/agent/contract.md` (line ~90).
- [ ] Re-run the skill mirror sync so `intrinsicengine-core` references carry the corrected text.

## Tests
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes.
- [ ] `python3 tools/agents/sync_skills.py --check` (from `PROC-001`) passes after resync.
- [ ] `python3 tools/agents/check_task_policy.py --root . --strict` passes.

## Docs
- [ ] `AGENTS.md` §10 and `docs/agent/contract.md` updated as above (this task is the docs change).
- [ ] No other documents reference warning-mode structural checks; verify with `grep -rn "warning mode" docs/ AGENTS.md` and fix any stragglers found.

## Acceptance criteria
- [ ] No contract document claims structural checks run in warning mode.
- [ ] The contract states the strict-by-default reality and the tracked-exception rule for future warning-mode checks.
- [ ] Skill mirrors match the corrected sources.

## Verification
```bash
grep -rn "warning mode" AGENTS.md docs/agent/ || true
python3 tools/agents/sync_skills.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Changing any workflow step or check mode in `.github/workflows/`.
- Loosening any currently strict check while "aligning" the text.
