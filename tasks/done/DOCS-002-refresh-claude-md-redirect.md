# DOCS-002 — Refresh `CLAUDE.md` redirect to current `docs/agent/` state

## Goal
- Bring the root `CLAUDE.md` redirect into agreement with what is actually checked in under `docs/agent/`: drop the "may be introduced incrementally" hedge for documents that now exist, normalize the link form, and list the full set of agent process docs rather than a stale three-file subset.

## Non-goals
- Do not move or rewrite `AGENTS.md` policy; `CLAUDE.md` remains a thin redirect per its own preamble.
- Do not edit any file under `docs/agent/` (no policy changes).
- Do not introduce new agent-process documents.
- Do not change `.github/copilot-instructions.md` or `.codex/config.yaml`.

## Context
- Status: done.
- Completed: 2026-05-19.
- Owner/agent: Claude on `claude/setup-agent-workflow-df3GL`.
- Commit: pending on `claude/setup-agent-workflow-df3GL` (PR: pending).
- Resolution: replaced the hand-maintained 3-entry list and the "may be introduced incrementally" hedge with a single-sentence pointer to `AGENTS.md` §"Related expanded docs", which is already the authoritative index of all 12 `docs/agent/*.md` process docs. Normalized the `/AGENTS.md` leading-slash references in the "Authoritative repository policy" block to the repo-relative `AGENTS.md` form at the same time, matching the convention used elsewhere in the repository.
- Owning subsystem/layer: repo-root policy/redirect doc; complements `AGENTS.md` per `AGENTS.md` §0 ("This file supersedes policy text in `CLAUDE.md`...").
- Drift (`CLAUDE.md:5-16` before this task):
  - Stated that `docs/agent/contract.md`, `docs/agent/task-format.md`, and `docs/agent/review-checklist.md` "may be introduced incrementally." All three exist and are ~66–106 lines of substantive content.
  - Used absolute-style link form (`/docs/agent/...`) rather than the repo-relative form (`docs/agent/...`) used elsewhere in `AGENTS.md` and the rest of `docs/`.
  - Listed only 3 of the 13 files in `docs/agent/`. Missing references included `architecture-review-checklist.md`, `benchmark-workflow.md`, `benchmark-review-checklist.md`, `docs-sync-policy.md`, `method-workflow.md`, `method-review-checklist.md`, `roles.md`, `task-maturity.md`, `agent-output-review-checklist.md`.

## Required changes
- [x] Update [`CLAUDE.md`](../../../CLAUDE.md) "Agent process references" block to:
  - Use repo-relative link form (`docs/agent/<name>.md`, no leading slash).
  - Replace the "may be introduced incrementally" sentence with a one-line pointer to `AGENTS.md` §"Related expanded docs", which is already the authoritative index.
- [x] Either list every file under `docs/agent/` (mirroring the table in `AGENTS.md`), or, preferably, replace the hand-maintained list with a single sentence: "See `AGENTS.md` §Related expanded docs for the authoritative index of agent process references." Single-pointer form was chosen to avoid future drift.
- [x] Keep the "Tooling note" final paragraph and the "Authoritative repository policy" pointer intact.

## Tests
- [x] `python3 tools/docs/check_doc_links.py --root .` passes (all `docs/agent/...` links from `CLAUDE.md` resolve). Result: 508 relative links checked, no broken links.
- [x] `python3 tools/repo/check_root_hygiene.py --root .` passes. Result: `CLAUDE.md` allowed.
- [x] `python3 tools/agents/validate_tasks.py --root tasks --strict` passes. Result: 261 task files validated, 0 findings.

## Docs
- [x] No `docs/agent/` files were modified by this task.
- [x] `.github/copilot-instructions.md` and `.codex/config.yaml` still point at `AGENTS.md` and do not contradict the updated `CLAUDE.md`; they were not touched.

## Acceptance criteria
- [x] `CLAUDE.md` no longer claims any `docs/agent/*.md` file "may be introduced incrementally." Verified: `! grep -nE 'introduced incrementally' CLAUDE.md` returns no matches.
- [x] `CLAUDE.md` no longer uses leading-slash link form for repo-relative paths. Verified: `! grep -nE '\(/docs/agent/' CLAUDE.md` returns no matches; `/AGENTS.md` references are also normalized to `AGENTS.md`.
- [x] The file delegates to `AGENTS.md` §"Related expanded docs" with a single sentence.
- [x] The diff is scoped to `CLAUDE.md` (no touches to `.github/copilot-instructions.md`, `.codex/config.yaml`, or any `docs/agent/*.md` file).

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/agents/validate_tasks.py --root tasks --strict

# Sanity: confirm CLAUDE.md no longer uses absolute-style doc/agent links
! grep -nE '\(/docs/agent/' CLAUDE.md
# Sanity: confirm CLAUDE.md no longer hedges that contract docs are incremental
! grep -nE 'introduced incrementally' CLAUDE.md
```

## Forbidden changes
- Editing any file under `docs/agent/`.
- Re-introducing policy text into `CLAUDE.md` (it must remain a thin redirect).
- Touching `AGENTS.md` from this task.
