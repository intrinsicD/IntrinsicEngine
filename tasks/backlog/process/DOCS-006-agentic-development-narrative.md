---
id: DOCS-006
theme: H
depends_on: []
---
# DOCS-006 — Curated "how this repo is built" agentic-development narrative

## Goal
- Write one curated document that explains the agentic development system to
  an outside reader (portfolio audience): task lifecycle
  (backlog → active → done → archive), convergence themes, skills, validators,
  and CI gates — with one real task traced end-to-end from seeding to
  retirement-log entry.

## Non-goals
- No new process rules; the document describes the system, it does not
  extend it.
- No duplication of `AGENTS.md` content — link, don't restate (the contract
  stays authoritative).

## Context
- 2026-07-14 process review: the raw record (661 archived tasks,
  RETIREMENT-LOG, 21 skills, validator fleet) is testimony but not readable
  as a portfolio artifact; a curated narrative is the presentable surface.
- Owner: `docs/agent/`, root `README.md` link.

## Required changes
- [ ] Author `docs/agent/how-this-repo-is-built.md`: system overview,
  lifecycle diagram, skill/validator tiers, one traced example task
  (seed → slice → verification → retirement → archive).
- [ ] Link it from the root `README.md` and the `docs/agent/` index in
  `AGENTS.md` §"Related expanded docs".

## Tests
- [ ] `check_doc_links` passes; docs-sync validator satisfied.

## Docs
- [ ] This task is the docs change; skill mirrors resynced if
  `docs/agent/*` routing tables change.

## Acceptance criteria
- [ ] A reader who knows none of the repo conventions can explain the task
  lifecycle and where to find evidence for any retired claim after reading
  the one document.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root .
```

## Forbidden changes
- Engine code changes.
- Moving or rewriting archived task files.
