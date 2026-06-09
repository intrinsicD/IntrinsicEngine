# PROC-007 — Onboarding prompt tightening and loop-mode defaults

## Goal
- Tighten `docs/agent/prompt/prompt.md` to session-procedural content only: replace rule blocks duplicated from `AGENTS.md` §5/§12 with links to the contract, give the multi-task loop mode explicit defaults and a per-iteration checkpoint rule, and normalize the stop-condition list formatting.

## Non-goals
- No policy changes — every rule keeps its current meaning; only its authoritative home changes.
- No changes to the reading order / session-brief adoption (owned by `PROC-004` Slice B).
- No changes to `AGENTS.md` itself beyond none-at-all: the contract already owns these rules.
- No new prompt sections or workflow steps.

## Context
- Owner/layer: onboarding prompt (`docs/agent/prompt/prompt.md`) and its skill mirror (`intrinsicengine-core/references/session-onboarding.md`). No engine code.
- Findings from the 2026-06-09 prompt review:
  - The "Implement the smallest robust slice" and "Commit and PR hygiene" sections restate `AGENTS.md` §5 and §12/commit-hygiene almost verbatim; the `.cppm` interface/implementation rule now exists in three places with drifting wording. Duplication means every policy edit must touch multiple files or silently diverge.
  - Loop mode references "more than N tasks have completed" and "runtime exceeds the configured budget" without defaults, forcing agents to guess; and it never instructs committing/pushing between iterations, so an interrupted loop in an ephemeral environment (e.g. remote/web sessions) loses all completed work since the last push.
  - The stop-condition list mixes `,` and `.` bullet terminators — trivial, but the file is pasted verbatim into agent context.
- The prompt mandates reading `/AGENTS.md` every session, so links to contract sections lose no information while halving the prompt's maintenance surface.
- Depends on `PROC-001` (edits a mirrored doc; sync must be trustworthy). Independent of `PROC-004` — the two tasks edit disjoint prompt sections; whichever lands second rebases trivially.

## Required changes
- [ ] In "Implement the smallest robust slice", keep only session-procedural guidance (smallest-slice framing, CPU/null-path preservation pointer, test-label pointer) and replace the bullets that restate `AGENTS.md` §5 — including the full `.cppm` interface/implementation paragraph — with one line linking the contract section, so the rule has exactly one authoritative wording.
- [ ] In "Commit and PR hygiene", same treatment against `AGENTS.md` §12 and the commit-hygiene rules: keep retire/promote sequencing notes that are prompt-specific, link the rest.
- [ ] In "Multi-task loop mode", state defaults inline: when the operator has not configured them, stop after `N = 3` completed tasks and treat the runtime budget as not-set (rely on the other stop conditions); both remain operator-overridable in the invoking prompt.
- [ ] In "Multi-task loop mode", add a checkpoint rule: after each iteration that retires or materially advances a task, commit and — when a remote branch is configured — push before starting the next iteration, so interrupted loops lose at most one iteration.
- [ ] Normalize the stop-condition list to consistent bullet punctuation.
- [ ] Re-run the skill mirror sync so `session-onboarding.md` matches the tightened prompt.

## Tests
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes (new contract-section links resolve).
- [ ] `python3 tools/agents/sync_skills.py --check` (from `PROC-001`) passes after resync.
- [ ] `python3 tools/agents/check_task_policy.py --root . --strict` passes.
- [ ] Spot-check: `grep` the `.cppm` rule's distinctive phrases (e.g. "allocation-heavy work") in `docs/agent/prompt/prompt.md` returns nothing — the wording lives only in `AGENTS.md` and its direct mirrors.

## Docs
- [ ] `docs/agent/prompt/prompt.md` updated as above (this task is the docs change).
- [ ] Skill mirror resynced via the `PROC-001` tooling.

## Acceptance criteria
- [ ] No rule block appears verbatim in both `prompt.md` and `AGENTS.md` §5/§12; the prompt links instead.
- [ ] Loop mode has explicit, operator-overridable defaults for `N` and the runtime budget, plus the per-iteration commit/push checkpoint rule.
- [ ] Stop-condition list formatting is uniform.
- [ ] Prompt length decreases while every current behavior remains reachable through the contract links.

## Verification
```bash
grep -n "allocation-heavy work" docs/agent/prompt/prompt.md; test $? -ne 0
python3 tools/agents/sync_skills.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Changing the meaning, strictness, or scope of any rule while relocating its wording.
- Editing the reading-order section (owned by `PROC-004`) or `AGENTS.md`.
