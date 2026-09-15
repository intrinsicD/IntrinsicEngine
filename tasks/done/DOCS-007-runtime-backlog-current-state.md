---
id: DOCS-007
theme: H
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-overnight"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-15T02:24:34Z"
contract_schema: 1
contracts: [repo.source-documentation]
---
# DOCS-007 — Make runtime backlog discovery current and concise

## Goal
Replace duplicated retirement narratives and stale prerequisite claims in the
runtime backlog README with concise, accurate discovery of currently open work.

## Non-goals
No engine source, test, CMake, policy, skill, task-scope/dependency or historical
record change. No new index, generator, helper, research or performance claim.

## Context
User-directed cleanup with Claude until 2026-09-15 08:00 Europe/Berlin;
stop new implementation by 07:15, local commits only. This discovery cleanup is
explicitly within the user's reuse/complexity request. Claim after RUNTIME-256
retires/seals and its writer/builds finish. Standing Claude authorization applies.

`tasks/backlog/runtime/README.md` has 1,209 lines, only five local open task
links, 46 done links and 84 archive links. Its old narrative still describes
HARDEN-087, UI-039, REVIEW-003, CORE-005..009, ARCH-006 and METHOD-019/020 as
pending in places; these are in `tasks/done`, while BUG-055 is archived.
The current task files and existing retirement log already own exact scope,
state, dependency and completion details. Do not replicate those records.

Retain the five local tasks RUNTIME-210/211/212/218/222 with short ownership
summaries, the existing architecture/geometry/rendering backlog entry links,
and useful current contract/session-brief entrypoints. Preserve resolved open
link identities and complete local task coverage at the actual edit time.
Remove hardcoded counts, retired-gate claims, repeated completed-task lists,
chronology and speculative deferred roadmaps. Link `tasks/done/RETIREMENT-LOG.md`,
`tasks/done/README.md` and `tasks/archive/README.md` in one brief paragraph;
these files exist. Avoid History/Retired/Deferred README headings, which the
existing source-documentation policy/auditor intentionally rejects.

Claude's first read-only draft was not accepted as-is: it retained several
stale gates and a deferred roadmap. The corrected plan uses actual task file
locations and current repo.source-documentation policy. Keep old records intact.
Root accepts Claude's four narrowing amendments: no hardcoded count; link the
methods queue rather than repeating individual future integration IDs; use the
verified `docs/architecture/index.md` entrypoint rather than a copied ADR table; keep history
links in a brief paragraph with no history-dedicated heading. Link active tasks
and the session brief without copying the current active task into this backlog.

Reuse/right-sizing: canonical task files plus existing indexes/logs provide
all needed ownership and history; no replacement data structure or generator.

## Required changes
- [x] Rewrite only the runtime backlog README into a concise current entry point.
- [x] Keep all actual open local tasks and existing open backlog category links discoverable.
- [x] Remove completed-gate claims and duplicated history while linking its existing owners.

## Tests
- [x] Save baseline/final line counts, open-link identities and local task coverage.
- [x] Source-documentation audit and strict task-state/doc/task/root checks pass.
- [x] Claude reviews the final fixed text against actual current task locations.

## Docs
- [x] Retire with exact local source identity and standard completion evidence.

## Acceptance criteria
- [x] README provides concise current task and contract discovery without retired-gate claims.
- [x] Open task coverage and existing historical records are preserved with passing checks and Claude review.
- [x] Docs-only slice is locally committed, retired and sealed with no source/build/behavior change.

## Verification
```bash
python3 tools/agents/skills/intrinsicengine-source-documentation/scripts/audit_source_documentation.py --root . --path tasks/backlog/runtime/README.md --summary
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/agents/generate_session_brief.py --check
python3 tools/agents/workflow_evidence.py validate --root .
```
Record the pre-edit README audit as optional baseline evidence, then require
its final audit to pass. No new persistent regression test for an editorial
rewrite. C++/sanitizer/GPU builds are not repeated because no executable,
interface, test, build input or runtime configuration changes; RUNTIME-256
owns the preceding source verification. No timing measurements are made.

## Forbidden changes
- Closing or changing scope of open task files merely to make the index shorter.
- Deleting archived/done task records or copying them into another new index.
- Broad changes to other backlog READMEs, source docs, agents, policy or code.

## Validator finding — retirement navigation
The initial strict task-state check classified links to the done/archive README
indexes as retired task entries and required a history heading. That conflicts
with the current source-documentation rule permitting brief navigation while
rejecting history sections. `validate_category_indexes` checks only whether a
resolved path is beneath done/archive; it does not distinguish index navigation
from task membership. Directory links have the same false positive. BUG-196 owns
the narrow checker repair and restoration of clickable index navigation.

This slice keeps the existing supported retirement-log link and cites both index
paths plainly, as the checker permits. Open task links and historical records
remain preserved. The initial failing receipt stays bound as evidence; the final
checks must pass. No validator or policy is changed in DOCS-007.

## Completion — 2026-09-15
- Endpoint: **Retired**, current-state documentation cleanup.
- Commit: implementation and retirement are in the enclosing local commit;
  `tasks/evidence/DOCS-007/seal.yaml` binds the exact source revision.
- Runtime backlog README shrinks from 1,209 to 54 lines. All eight required
  open link targets and five local task files remain discoverable; 127 linked
  historical task records retain exact hashes. No executable, build or tool
  source changed. Existing task notes and indexes remain the authoritative owners.
- Source-documentation audit improves from 11 objective errors and 359 review
  notes to zero errors and six reviewed heuristic notes. Those six are the five
  necessary live task links and brief historical navigation, not duplicated
  chronology. Strict final doc/task/state/root checks pass.
- Claude approved the fixed final text and its match to the exact task goals.
  Root clarified ownership phrasing, retained the existing retirement-log link
  and plain index paths, and recorded BUG-196 for the checker false positive.
  Optional methods-queue navigation was not required by the preserved target set.
- The initial structural failure exposed BUG-196. The next preflight caught a
  missing required Context heading in that new bug note; fixed without changing
  the approved README. Both raw failures remain bound as artifacts, with the
  passing final check as the completion gate. No validator was weakened.
- No C++/sanitizer/GPU build or execution is claimed for this docs-only slice.
  The preceding RUNTIME-256 owns source verification. No research or timing claim;
  BUILD-007/C92 and the remaining engine/product work stay open.
