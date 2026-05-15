# METHOD-008 — Recurring method SOTA review (standing task)

## Goal
- Maintain a quarterly review cadence that keeps the engine's default method backends current with the published state of the art and retires superseded variants under a controlled migration policy.

## Non-goals
- This task does **not** implement any algorithm itself. Implementation work is delegated to follow-up tasks created during each review cycle.
- This task is not retired when a single review cycle completes; it stays in `tasks/backlog/methods/` permanently and accumulates a `## Review log` of completed cycles.
- No deletions of existing variants happen here — the review only proposes follow-up tasks under the migration policy in [`docs/agent/method-sota-review.md`](../../../docs/agent/method-sota-review.md).
- No flipping of default variants in the same task as the variant introduction (that is a separate, explicit, audited task per migration policy step 3).

## Context
- Status: backlog (standing).
- Owning subsystem/layer: cross-cutting; primarily `methods/` and `src/geometry/`.
- Authoritative process doc: [`docs/agent/method-sota-review.md`](../../../docs/agent/method-sota-review.md).
- Cadence: once per calendar quarter, plus one targeted pass after each major venue proceedings (SIGGRAPH, SIGGRAPH Asia, EUROGRAPHICS, SGP).
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) (the first such review).
- Initial scope at task-creation time covers the eight method tasks introduced alongside this one:
  - [`METHOD-002`](METHOD-002-signed-heat-method-reference-backend.md)
  - [`METHOD-003`](METHOD-003-closest-point-method-pde-reference-backend.md)
  - [`METHOD-004`](METHOD-004-walk-on-spheres-reference-backend.md)
  - [`METHOD-005`](METHOD-005-robust-mesh-boolean-reference-backend.md)
  - [`METHOD-006`](METHOD-006-cross-field-design-reference-backend.md)
  - [`METHOD-007`](METHOD-007-constrained-delaunay-tetrahedralization-reference-backend.md)
  - [`GEOM-013`](../geometry/GEOM-013-feature-preserving-dual-contouring.md)
  - [`GEOM-014`](../geometry/GEOM-014-feature-aware-quadric-error-simplification.md)

## Required changes

These checkboxes describe the work performed **per review cycle**. After completing a cycle, do **not** mark them permanently complete; instead, append a dated entry to the `## Review log` section below and reset the cycle checkboxes for the next cycle.

- [ ] Open the cycle by creating a fresh dated review note `docs/reviews/<YYYY-MM-DD>-method-sota-review.md` from the template structure of `docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`.
- [ ] Enumerate every `METHOD-*` task in `tasks/active/` and `tasks/backlog/` and every method package under `methods/`. Verify each still has a marked default in its `## Variants and default selection` section.
- [ ] For each method, scan the latest quarter's arxiv `cs.GR` and the most recent venue proceedings. Identify candidate replacement variants.
- [ ] Apply the decision rubric in [`docs/agent/method-sota-review.md`](../../../docs/agent/method-sota-review.md). Record the decision in the review note (keep / replace / defer) with explicit citations.
- [ ] For each "replace" decision, create one follow-up task under the appropriate `tasks/backlog/<domain>/` directory using the existing ID conventions. Link the follow-up task in the review note.
- [ ] Append a dated entry to this task's `## Review log` section summarising the cycle.
- [ ] Reset the cycle checkboxes (uncheck the seven items above) so the next cycle starts clean.

## Tests
- [ ] Each cycle: run `python3 tools/docs/check_doc_links.py --root .` after creating the review note and any follow-up tasks.
- [ ] Each cycle: run `python3 tools/agents/check_task_policy.py --root . --strict` after creating follow-up tasks.
- [ ] No C++ tests are required for this task itself.

## Docs
- [ ] Each cycle: a dated review note at `docs/reviews/<YYYY-MM-DD>-method-sota-review.md`.
- [ ] Each cycle: an entry in the `## Review log` below.
- [ ] If the process changes, update [`docs/agent/method-sota-review.md`](../../../docs/agent/method-sota-review.md) in a separate commit with a clear rationale.

## Acceptance criteria

These define a **single completed cycle**. Because this task is standing, the criteria reset between cycles.

- [ ] A dated review note exists under `docs/reviews/`.
- [ ] Every method in scope has an explicit keep / replace / defer decision recorded.
- [ ] All "replace" decisions have corresponding follow-up tasks in `tasks/backlog/`.
- [ ] No default variant has been flipped in the same cycle as a new variant's introduction (migration policy step 3 enforced).
- [ ] Doc-link and task-policy checks pass.
- [ ] A dated entry is appended to `## Review log`.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not implement any algorithm in this task.
- Do not delete or rewrite an existing method package or variant.
- Do not flip a default variant within this task.
- Do not move this task to `tasks/done/`; it is permanent.
- Do not introduce a neural variant as the default unless the engine has shipped a non-neural reference for the same problem.

## Review log

Each completed cycle appends one dated entry. Do not edit prior entries.

<!-- Example entry; remove this comment after the first real cycle is logged. -->
<!--
### 2026-08-15

- Reviewed: METHOD-002, METHOD-003, METHOD-004, METHOD-005, METHOD-006, METHOD-007, GEOM-013, GEOM-014.
- Decisions: keep all current defaults; defer one candidate (cite arxiv id) for next cycle.
- Follow-up tasks created: none.
- Review note: docs/reviews/2026-08-15-method-sota-review.md.
-->
