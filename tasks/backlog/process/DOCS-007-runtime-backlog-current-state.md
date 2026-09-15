---
id: DOCS-007
theme: H
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
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
- [ ] Rewrite only the runtime backlog README into a concise current entry point.
- [ ] Keep all actual open local tasks and existing open backlog category links discoverable.
- [ ] Remove completed-gate claims and duplicated history while linking its existing owners.

## Tests
- [ ] Save baseline/final line counts, open-link identities and local task coverage.
- [ ] Source-documentation audit and strict task-state/doc/task/root checks pass.
- [ ] Claude reviews the final fixed text against actual current task locations.

## Docs
- [ ] Retire with exact local source identity and standard completion evidence.

## Acceptance criteria
- [ ] README provides concise current task and contract discovery without retired-gate claims.
- [ ] Open task coverage and existing historical records are preserved with passing checks and Claude review.
- [ ] Docs-only slice is locally committed, retired and sealed with no source/build/behavior change.

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
