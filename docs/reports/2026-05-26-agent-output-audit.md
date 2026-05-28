# Agent-output Weekly Audit — 2026-05-26

Second cadence audit against
[`docs/agent/agent-output-review-checklist.md`](../agent/agent-output-review-checklist.md).
Follows the
[2026-05-17 calibration audit](2026-05-17-agent-output-audit.md);
window extended to cover the missed 2026-05-24 slot, per the cadence rule
that "missing a week is not a failure — the next reviewer extends the
window".

## Window

- Date range: 2026-05-18 → 2026-05-26 (8 days; 84 non-merge commits).
- Tasks covered (10):
  [`GRAPHICS-074`](../../tasks/done/GRAPHICS-074-default-recipe-selection-outline-and-picking-readback.md)
  (retired 2026-05-21 after Slice D.4),
  [`GRAPHICS-075`](../../tasks/done/GRAPHICS-075-default-recipe-postprocess-chain-wiring.md)
  (retired 2026-05-22 after Slice E.2 fixup),
  [`GRAPHICS-076`](../../tasks/done/GRAPHICS-076-default-recipe-debug-view-and-present-wiring.md)
  (now retired; at audit time Slices A/B/C had landed and Slice D was blocked on Vulkan host),
  [`GRAPHICS-077`](../../tasks/active/GRAPHICS-077-transient-debug-primitive-upload-helper.md)
  (active; Slices A/B/C landed, Slice D blocked on Vulkan host),
  [`GRAPHICS-078`](../../tasks/active/GRAPHICS-078-visualization-overlay-upload-helper.md)
  (active; Slices A/B/C landed, Slice D blocked on Vulkan host),
  [`RUNTIME-082`](../../tasks/active/RUNTIME-082-spatial-debug-adapters.md)
  (active; Slices A + B landed),
  [`GEOM-006`](../../tasks/done/GEOM-006-indexed-mesh-soup-conversion-contracts.md)
  (retired 2026-05-21),
  [`GEOM-007`](../../tasks/done/GEOM-007-robust-predicates-intersection-classification.md)
  (retired 2026-05-22 after Slice 3.3.c),
  [`GEOM-015`](../../tasks/done/GEOM-015-gjk-termination-diagnostics.md)
  (retired 2026-05-22 after Slice 4),
  [`REVIEW-002`](../../tasks/backlog/architecture/REVIEW-002-recurring-drift-and-inconsistency-audit.md)
  (backlog seed for the recurring drift audit).
- Sampling: rows 1–9 each spot-checked against 3–5 representative
  substantive commits per task family (geometry, graphics passes,
  graphics upload helpers, runtime adapters, agent-skills/process).
  Heavy weight on the four new module surfaces introduced this window:
  `ISpatialDebugAdapter`, `ITransientDebugUploadHelper`,
  `IVisualizationOverlayUploadHelper`, and the postprocess pass split.

Notable substantive commits used as evidence anchors:

- `abd0f4b` RUNTIME-082 Slice B — add KdTree + Octree spatial debug adapters.
- `77738f2` + `e8717be` RUNTIME-082 Slice A — scaffold + temp-rejection ctor.
- `fb72656` GRAPHICS-078 Slice C — wire isoline overlay recording.
- `fd56341` GRAPHICS-078 Slice B fixup — overflow-gate before staging alloc.
- `ab991a4` GRAPHICS-077 Slice C — only flip `Recorded` when upload succeeded.
- `e26368a` GRAPHICS-077 Slice C — wire line + point lanes.
- `21be263` GRAPHICS-076 Slice B — wire canonical `Pass.DebugView` operationally.
- `e33c9b3` GRAPHICS-076 Slice C — compile-path Backbuffer non-finalizer test.
- `bd986cf` + `73ffcad` GRAPHICS-075 Slice A — tonemap push payload align.
- `cd8f51a` + `04fda64` + `c91b832` GEOM-015 Slices 2–3 — GJK migrations.
- `1ac8720` GEOM-007 Slice 1 — `Geometry.RobustPredicates` foundation.
- `47b1e1f` Agent-skills batch (diagnose, zoom-out, handoff, task-workflow, review).

## Findings

| Row | Failure mode | Outcome | Evidence |
| --- | --- | --- | --- |
| 1 | Silent scope creep | pass | `git show --stat` on the sampled substantive commits keeps changes inside the named task's required-changes scope. `abd0f4b` (RUNTIME-082 Slice B) touches only `src/runtime/SpatialDebug/`, the matching contract test, and the slice's task-file status block. `fb72656` (GRAPHICS-078 Slice C) is bounded to `src/graphics/renderer/Graphics.VisualizationOverlayUploadHelper.{cpp,cppm}`, `Pass.VisualizationOverlay.{cpp,cppm}`, the renderer wiring file, the isoline shader pair, and the contract test. `cd8f51a` (GEOM-015 Slice 2) only touches the three GJK/Support/SDFContact migration call-sites and their tests. No drive-by edits in unrelated layers. |
| 2 | Decorative comments and docstrings | pass | New comment blocks on `ISpatialDebugAdapter`/`ITransientDebugUploadHelper`/`IVisualizationOverlayUploadHelper` document cross-pass contracts (per-lane independence, BDA-fetch shader vertex layout, buffer-recycling invariants) and ownership/lifetime — these are real WHY notes, not WHAT restatements. The `BvhAdapter(const Geometry::BVH&&) = delete` deletion is documented with a one-line dangling-temporary rationale, exactly the kind of WHY-comment AGENTS.md calls for. Headers add per-field-cluster context (`// GRAPHICS-077 Slice C — independent per-lane buffer leases ...`) but each cluster is followed by a behavioral invariant the cluster encodes; no internal helpers carry multi-paragraph docstrings restating their parameters. |
| 3 | Premature abstraction | pass | Three new interfaces (`ISpatialDebugAdapter`, `ITransientDebugUploadHelper`, `IVisualizationOverlayUploadHelper`) were added this window; each clears the "two adapters" bar. `ISpatialDebugAdapter` has three concrete implementations in tree by end of window (`BvhAdapter`, `KdTreeAdapter`, `OctreeAdapter`) plus a fourth (`ConvexHullAdapter`) explicitly planned in [`RUNTIME-082`](../../tasks/active/RUNTIME-082-spatial-debug-adapters.md) Slice C. `ITransientDebugUploadHelper` and `IVisualizationOverlayUploadHelper` each have a default in-renderer impl plus a Vulkan-tuned variant explicitly planned in the task files' Slice D under the `gpu;vulkan` host gate — the header comments cite that planned second implementation directly. No `Make*`/`Build*` factory helpers, no single-method interfaces without callers. `Geometry.RobustPredicates` adds free functions for shared numerics (3 call-sites migrate in the same task), not a wrapper layer. |
| 4 | Documented-but-not-tested | pass | Every behavioral doc claim has a corresponding test. The "per-lane independent geometric growth" claim in `Graphics.TransientDebugUploadHelper.cppm` is pinned by `PerFrameBufferRecycling*` in `Test.TransientDebugSurfacePass.cpp` (16 tests, 1148 lines). The isoline lane recording shape in `Pass.VisualizationOverlay.cppm` is exercised by `Test.VisualizationOverlayPass.cpp` (14 tests, 1054 lines). The `BackbufferWrittenByNonFinalizer` finding documented in `src/graphics/renderer/README.md` is asserted end-to-end by `Test.RenderGraphValidation.cpp::CompileBackbufferWrittenByNonFinalizerReportsStructuredFinding` (added in `e33c9b3` as Slice C's contract). `ApproxZeroSq`/`ApproxZeroLen` policy claims in `RobustPredicates` are covered by the six new `RobustPredicatesApproxZero*` cases in `Test.RobustPredicates.cpp`. The OctreeAdapter `SplitPoint::Center` exact / `Mean`/`Median` approximate note in `Runtime.SpatialDebugAdapters.cpp:272-278` is exercised against the Center policy by `Test.SpatialDebugAdapters.cpp` (10 tests, 456 lines). |
| 5 | Defensive validation at internal boundaries | findings | Three new modules in this window initialise non-owning pointers from constructor reference parameters (`BvhAdapter::m_Bvh`, `KdTreeAdapter::m_KdTree`, `OctreeAdapter::m_Octree`; `TransientDebugUploadHelper::m_Device`/`m_BufferManager`; `VisualizationOverlayUploadHelper::m_Device`/`m_BufferManager`), then guard the public methods with an `m_X == nullptr` early-return (`Runtime.SpatialDebugAdapters.cpp:61,163,259`; `Graphics.TransientDebugUploadHelper.cpp:173,229,289`; `Graphics.VisualizationOverlayUploadHelper.cpp:176,274`). The reference-binding constructors plus the deleted rvalue overloads on the adapters make the pointers non-null for the lifetime of the helper; no test exercises the null branch, and the rvalue-rejection contract test verifies the constructor never accepts a temporary. These are textbook Row-5 internal-boundary checks: the precondition is already guaranteed by the caller (renderer composition root for the upload helpers; runtime adapters constructed from a live tree reference). The pattern is consistent across all three new modules — suggesting a shared scaffolding habit rather than a one-off slip. |
| 6 | Untracked compatibility shims | pass | `git log --since=2026-05-18 -p --no-merges -- '*.cpp' '*.cppm' '*.h'` filtered for added lines containing `TODO\|FIXME\|deprecated\|backcompat\|temporary shim` returned no new shim/back-compat phrasing in source. The only `TODO`-shaped additions are inside `docs/agent/agent-output-review-checklist.md` (`47b1e1f`) and `tasks/backlog/legacy-todo.md` cross-links — both documentation of the audit pattern itself, not in-tree shims. No new entries appear in `tools/repo/layering_allowlist.yaml`. |
| 7 | Ceremony without shipped value | pass | 84 commits in window; ~70 are substantive code-and-test commits (geometry predicates + classification, postprocess chain split, picking readback ownership, three new upload helpers, three new spatial adapters) and ~14 are task/docs maintenance (promotes, retires, status refreshes, skill additions). Substantive commits collectively retired 5 tasks (GRAPHICS-074, GRAPHICS-075, GEOM-006, GEOM-007, GEOM-015), advanced 4 active tasks through their CPU/null-contracted slices (GRAPHICS-076, GRAPHICS-077, GRAPHICS-078, RUNTIME-082), and opened the REVIEW-002 recurring-drift backlog seed — measurable engine progress. The agent-skills batch (`47b1e1f`) added five new specialist skills (diagnose, zoom-out, handoff, task-workflow extension, review extension) wired into the routing table; not pure ceremony — directly callable by subsequent sessions. |
| 8 | Half-finished implementations | pass | Every new public symbol in this window has a non-test call site or an end-to-end test in the same commit. `BvhAdapter`/`KdTreeAdapter`/`OctreeAdapter` are exercised by 10 cases in `Test.SpatialDebugAdapters.cpp` and consumed by the same builder seam in `Extrinsic.Graphics.SpatialDebugVisualizers`. `TransientDebugUploadHelper::UploadTriangles`/`UploadLines`/`UploadPoints` are invoked from `Graphics.Renderer.cpp::RecordTransientDebugSurfacePass` (the executor branch in `Pass.TransientDebug.Surface`) and exercised by 16 contract tests including the `OnlyFlipsRecordedWhenUploadSucceeded` case added in `ab991a4`. `VisualizationOverlayUploadHelper::UploadVectorFields`/`UploadIsolines` are invoked from `RecordVisualizationOverlayPass` and exercised by 14 contract tests. `Geometry::RayTriangle_Classify` (`5ab37c7`) migrates the Runtime.Selection picks call-site in the next slice (`6471de5`), keeping the seam exercised end-to-end. No orphan public symbols. |
| 9 | Aspirational documentation without `(planned)` marker | pass | The active tasks (GRAPHICS-076/077/078, RUNTIME-082) consistently mark un-landed work with explicit `Slice D blocked on Vulkan-capable host`, `Slice C remains`, or `the Vulkan-tuned variant in Slice D substitutes ...` phrasing — these are factual current-state markers, not present-tense claims. `src/graphics/renderer/Graphics.VisualizationOverlayUploadHelper.cppm` differentiates the CPU/null contract path (current) from the Vulkan-tuned variant (planned) in its lifetime comment. Retired-task READMEs (GRAPHICS-074, GRAPHICS-075, GEOM-006, GEOM-007, GEOM-015) replace prior `(planned)` markers with current-state assertions backed by the tests that landed alongside. No new present-tense claim was found whose code does not deliver. |

## Follow-ups

- **Row 5 (defensive internal-boundary null checks).** Filed as
  [`HARDEN-070`](../../tasks/backlog/architecture/HARDEN-070-drop-dead-null-guards-on-reference-initialised-helpers.md).
  The pattern repeats across three new modules
  (`SpatialDebugAdapters`, `TransientDebugUploadHelper`,
  `VisualizationOverlayUploadHelper`) and is a low-risk mechanical
  removal — the constructor-reference parameter is the precondition that
  makes the null check unreachable in well-formed code. The task drops
  the ~7 dead guards and adds a one-line lifetime-contract note in each
  helper's header so future readers understand why the null branch is
  absent.

  Until `HARDEN-070` lands, no per-PR action is required — the guards do
  not change observable behavior; the audit records the pattern so the
  next slice on those modules does not propagate it further.

- Row 9 is borderline rather than failing. The `VisualizationIsolinePushConstants::Reserved`
  field (and its `VisualizationVectorFieldPushConstants` sibling) is a
  legitimate std140-alignment pad, not a half-finished seam — the comment
  at `Pass.VisualizationOverlay.cppm:30-32` already documents the
  per-kind-evolution rationale ("e.g. per-glyph width or per-iso polyline
  expansion push fields in a follow-up task"). No follow-up needed; flagged
  here only so the next audit does not re-flag it.

## Calibration note

The window is ~9× larger than the calibration audit (84 commits vs. 9),
so the rows were spot-checked against representative substantive
commits per task family rather than walked commit-by-commit. The
checklist held up at that sampling rate — every row was decidable from
`git show --stat`, `git log -p` greps, and targeted reads against the
new module surfaces. Row 5 produced the first non-calibration finding
of the cadence and is a low-risk mechanical clean-up; no per-PR gate
change is implied. Row 8 (the row the calibration audit predicted
would most often produce findings, alongside Row 5) stayed clean —
the per-slice plans in GRAPHICS-076/077/078 and RUNTIME-082 each
required the slice's seam to be exercised by a contract test in the
same commit, which kept new public symbols from going orphan.

Future cadences should keep the windowing aligned to the calendar
week when possible; the 8-day extension here was a one-time catch-up.

## Elapsed time

- Start: 2026-05-26T07:16:03Z.
- Finish: 2026-05-26T07:21Z.
- Total: ≈ 5 minutes for the audit-only sweep (well under the
  60-minute target). Authoring the `HARDEN-070` follow-up task seed,
  the architecture-backlog README cross-link, and this report
  took another ≈ 5 minutes of docs-only work in the same session.
  The audit was tractable inside the calibration budget despite the
  9× larger window because Rows 1–9 were each decidable from
  `git show --stat` + `git log --since … -p`-style greps against the
  four new module surfaces (`ISpatialDebugAdapter`,
  `ITransientDebugUploadHelper`, `IVisualizationOverlayUploadHelper`,
  postprocess pass split) without deep code exploration.
