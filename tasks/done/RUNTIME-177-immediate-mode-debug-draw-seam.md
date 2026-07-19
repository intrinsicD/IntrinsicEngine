---
id: RUNTIME-177
theme: B
depends_on:
  - RUNTIME-166
  - RUNTIME-181
completed: 2026-07-19
---
# RUNTIME-177 — Immediate-mode debug-draw seam for runtime integrations

## Goal
- Decide whether a present production caller earns a generic runtime-owned
  immediate-mode debug-draw accumulator and helper API.

## Non-goals
- No synthetic Sandbox primitive or migration of an existing typed producer
  merely to manufacture a caller.
- No accumulator, service publication, config/UI field, renderer change,
  module, registry, interface, or test-only production surface without a
  present consumer.
- No removal of the backend-neutral snapshot packet contract used by renderer
  and graphics tests.

## Context
- 2026-07 right-sizing audit: `RuntimeRenderSnapshotBatch::DebugLines/
  DebugPoints/DebugTriangles` have no direct runtime/app/method writer. The
  proposed convenience seam therefore had to pass the P1 present-consumer
  deletion test before implementation.
- The 2026-07-19 inventory found zero direct packet/helper writers under
  `src/runtime`, `src/app`, `methods`, or `benchmarks`. Graphics references are
  contract tests.
- Production spatial-debug and transform-gizmo paths already use distinct,
  semantically richer typed producers. Migrating either only to justify this
  API would duplicate or lose their ownership semantics.
- `render.debug_draw_enabled` could not circularly justify absent behavior:
  P3 requires co-equal controls for real tuning state, not a new control stack
  for a feature with no caller.
- Existing `RuntimeRenderSnapshotBatch::Debug*` spans remain a consumed
  backend-neutral renderer/test contract. This decision rejects only an
  unearned generic runtime producer surface.

## Status

- Completed on 2026-07-19 as a no-implementation right-sizing decision.
- Commit reference: this task-only retirement commit.
- No source, CMake, config, UI, test, or module-inventory change landed.
- A future producer-owned task may reopen the idea only with an exact
  production caller and the first caller in the same slice.

## Required changes
- [x] Inventory every direct debug-packet/helper writer in production runtime,
      app, method, and benchmark code.
- [x] Apply the ADR-0027/P1 deletion test and reject the proposed generic
      accumulator because it has no production lookup/submission consumer.
- [x] Reject spatial-debug, transform-gizmo, and gratuitous Sandbox primitives
      as synthetic callers; preserve their current typed ownership paths.
- [x] Record the reintroduction trigger and the correct frame-reset boundary
      for any future producer-owned slice.

## Tests
- [x] Confirm no production direct writer exists and no implementation file or
      public surface changes.
- [x] Run strict task, state-link, docs-sync, root-hygiene, and generated
      session-brief checks.

## Docs
- [x] Update live backlog summaries and the future GRAPHICS-124 non-goal to
      reflect that no RUNTIME-177 seam exists.
- [x] Record the no-implementation outcome in the retirement log.

## Acceptance criteria
- [x] No dead accumulator, service, config field, UI state, or helper module is
      introduced.
- [x] Existing typed spatial-debug/gizmo producers and renderer packet
      contracts are unchanged.
- [x] `REVIEW-003` can treat RUNTIME-177 as a satisfied dependency without
      admitting a test-only public surface.
- [x] A future slice must name the exact main-thread pre-extraction producer,
      explain why typed paths are unsuitable, and land that first production
      caller with the seam.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root . --strict
git diff --check
```

## Forbidden changes
- Treating contract tests, a new toggle, or a future research idea as a
  production consumer.
- Rerouting spatial-debug or transform-gizmo packets solely to justify a new
  generic abstraction.
- Clearing a future accumulator during extraction. If a caller later earns
  the seam, clear once at accepted-frame start before all producers; historical
  checkpoint `7a1538b4` showed extraction-time clearing erases submissions.

## Maturity
- Retired decision only; no capability maturity is claimed.
