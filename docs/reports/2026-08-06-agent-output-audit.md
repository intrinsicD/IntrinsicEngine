# Agent-output audit — 2026-08-06

This fresh complete-interval audit follows
[`docs/agent/agent-output-review-checklist.md`](../agent/agent-output-review-checklist.md)
for [`REVIEW-003`](../../tasks/done/REVIEW-003-architecture-stability-right-sizing-readiness-audit.md).
It extends the missed weekly cadence all the way from the preceding report and
does not reuse the rejected REVIEW-003 audit.

## Window

- Commit range:
  `d33c62eebc48de5e8a65c50e057addf25f125c6b..51e7faddad943ab7727e407d008e474ec076566d`.
  The base commit published the
  [2026-05-28 audit](2026-05-28-agent-output-audit.md); the head is the clean
  REVIEW-003 baseline.
- Date range: 2026-05-28 through 2026-08-06.
- Volume: 1,709 commits including merges; 1,550 non-merge commits. Subject
  lines name 349 distinct task IDs. Another 719 use descriptive conventional
  subjects without an ID, so task-file/evidence linkage—not subject spelling—
  was used when those commits were sampled.
- Classification: 663 production-source commits, 782 test/benchmark commits,
  1,062 docs commits, 1,285 task commits, 181 tooling/infrastructure commits,
  and 201 evidence commits. Of the production-source commits, 607 include
  tests, 542 include docs, and 505 include both in the same commit.
- Sampling: quantitative ranking covered the complete range; manual review
  concentrated on the largest source deltas, commits touching five or more
  repository roots, added public/module surfaces, high-comment files, and
  added validation hot spots. Current-tree conclusions were cross-checked by
  the complete REVIEW-003 right-sizing and drift inventories.

Representative anchors include the legacy-tree retirement (`797e274c`),
renderer/runtime Theme B closure (`6443f4bb`), Engine service extraction and
its later right-sizing (`195fa78e`, `4ac0b463`), app-owned editor migration
(`ba307e67`), editor ownership localization (`d09d6893`), runtime source
organization (`61fec5c0`), and the final REVIEW-003 remediation leaves.

## Findings

| Row | Failure mode | Outcome | Evidence |
| --- | --- | --- | --- |
| 1 | Silent scope creep | pass | The largest and broadest commits were inspected by file/root. Large deltas are coherent migrations or evidence/data landings: legacy deletion, editor/runtime ownership moves, renderer contract delivery, runtime convergence, method packages, and task/evidence sealing. The 536k-line `0809cfd7` outlier is dominated by two declared model fixtures plus one research/backlog synchronization session, not an unrelated code cleanup. No recurring drive-by file family or unresolved current-tree consequence was found. |
| 2 | Decorative comments and docstrings | pass | Added-comment ranking identified `Graphics.Renderer.cpp`, `Runtime.Engine.cpp`, render extraction, geometry operations, and selection contracts as hot spots. The sampled blocks encode queue ownership, resource layout, teardown order, retained-span lifetime, or fail-closed behavior that is not recoverable from a signature. Some task IDs retain provenance, but the sample found no new internal parameter-by-parameter docstring pattern or unusually dense block that merely repeats self-describing code. |
| 3 | Premature abstraction | pass | Across the interval, 197 module interfaces were deleted and 130 added. The surviving complete inventory has ten `I*` interfaces: nine have multiple real production strategies/backends and `IRenderer` passes the volatile graphics/runtime compile-firewall deletion test. Role-named additions are either plain DTOs or carry lifecycle/concurrency/validation work. Rejected single-owner interfaces and helper bases were removed by `GRAPHICS-129..134`, `RUNTIME-216`, and `RUNTIME-217`; no public `*Bridge` remains. |
| 4 | Documented-but-not-tested | pass | 505 production+docs commits also contain tests. The 56 source-classified commits without tests in the same commit were inspected as a bounded candidate list; many are docs/method-custody or retirement/evidence commits classified by their path, and the remaining code fixes are covered by adjacent task slices and the current suite. Sampled behavioral claims for render recipes, scene replacement, domain borrows, and typed command routing resolve to exact tests. The fresh baseline passed 4,102 CPU-supported cases. |
| 5 | Defensive validation at internal boundaries | pass | The added-guard ranking was manually sampled. Most hot spots are UI/config/file/asset inputs, nullable registry/service lookups, numerical preflights, or GPU/backend failure boundaries. Those are explicit external/interface checks, not trusted-reference null guards. Earlier recurrences were retired by `HARDEN-070` and the final `RUNTIME-217` hook cleanup; no current dead reference-initialized guard family or duplicate validator remained. |
| 6 | Untracked compatibility shims | pass | The complete current-tree source-marker probe is clean. Retired compatibility paths and one-owner bridges were removed through the interval; remaining `temporary` uses describe storage/scratch objects. The permanent typed `PresentPass` finalization role is not a migration exception. No unowned deprecated alias, warning-mode escape, or time-unbounded compatibility branch remains. |
| 7 | Ceremony without shipped value | pass | The window contains 663 production and 782 test/benchmark commits; 607 production commits carry tests in the same commit. Task/docs/evidence volume is high because enrolled workflow custody landed during the interval, but it accompanies substantial engine, geometry, methods, graphics, runtime, and tooling delivery rather than replacing it. |
| 8 | Half-finished implementations | pass | Current exported and role-named surfaces were exhaustively checked for implementation and consumer/test file counts. The prior audit's dead timeline, pipeline registry, reconstructor base, upload-helper bases, and frame-loop hook family all have retired remediation tasks. `CurrentRendererContractAdapter` has three production consumers plus broad tests; `ImGuiAdapter` is owned by `EditorUiModule` and directly exercised. No unresolved test-only scaffold or self-only public seam remains. |
| 9 | Aspirational documentation without `(planned)` marker | pass | Strict docs sync is clean. Current architecture claims sampled in runtime, renderer, geometry, and frame-graph docs match source/tests. The only live future architecture document is explicitly titled `(planned)`, marked `Status: roadmap`, and bound to open `CI-012`; other marker hits are checklist/history prose. |

## Follow-ups

No findings and no new follow-up tasks. All right-sizing findings discovered by
the rejected REVIEW-003 attempt are retired, and the fresh source surface does
not reproduce them.

## Evidence limits

- The audit is complete over commit metadata and surface inventories but uses
  risk-ranked manual sampling for diff semantics, as the checklist intends.
- GPU/Vulkan and sanitizer execution are separate evidence classes. This audit
  relies on the fresh default CPU gate plus current-tree static review; the
  readiness report states the capability-specific limits.

## Elapsed time

Approximately 18 minutes (20:50–21:08 CEST). Full-range extraction and ranking
were automated; manual review stayed inside the checklist's 60-minute budget.
