# Agent-output audit — 2026-09-05

This on-demand complete-interval sweep follows
[`docs/agent/agent-output-review-checklist.md`](../agent/agent-output-review-checklist.md).
It starts at the exact head of the previous accepted output audit and ends at
the final BUG-165/166/167/168 integration candidate.

## Window

- Commit range:
  `51e7faddad943ab7727e407d008e474ec076566d..3ee6a343d13c54dbd255e15ce254aa812d6dc194`.
- Date range: 2026-08-06 through 2026-09-05.
- Volume: 231 commits including merges; 214 non-merge commits. The aggregate
  diff contains 1,991 changed-file entries, 222,359 insertions, and 5,697
  deletions.
- Classification (overlapping path classes): 49 production-source commits, 77
  test/benchmark commits, 40 documentation commits, 168 task-record commits,
  and 36 tooling/infrastructure commits. Of the 49 production-source commits,
  42 include tests in the same commit.
- Sampling: the complete range was ranked by size, repository-root spread,
  added public/module surfaces, new validation branches, and comment density.
  Manual review concentrated on the highest-ranked commits and every source
  commit without a same-commit test.

Large anchors include the two declared test fixtures (`ae0ca9765`,
`3b281c783`), curvature restoration and evidence (`caafade8e`,
`0f1330140`), signed-curvature segmentation (`be22bb456`), runtime source
organization (`61fec5c06`), the removal of the transient `src_new` tree
(`17d9d29a6`), and the final compile-hotspot identity repair
(`3ee6a343d`).

## Findings

| Row | Failure mode | Outcome | Evidence |
| --- | --- | --- | --- |
| 1 | Silent scope creep | pass | The largest deltas were inspected by commit and path. The two extreme additions are named point-cloud and mesh fixtures; the largest source/evidence commits remain coherent with their geometry, runtime, test, method, and custody tasks. The temporary `src_new` churn has no current-tree consequence because `17d9d29a6` removes that tree. No recurring drive-by file family was found. |
| 2 | Decorative comments and docstrings | findings | The report-only source-documentation inventory scans 1,329 files and reports 413 objective errors plus 3,891 review prompts, down from the initial PROC-033 baseline of 436 objective errors across 1,319 files. The window nevertheless adds five module interfaces without the mandatory leading what-and-why synopsis: `Geometry.HalfedgeMesh.CurvatureSegmentation{,.Features,.Patches}.cppm`, `Geometry.HalfedgeMesh.Features.cppm`, and `Runtime.CurvatureSegmentationConfig.cppm`. A focused scan reports exactly five errors. Six comments in `Runtime.GeometryProcessingOperations.Mesh.cpp` also narrate BUG/retirement history where a concise current invariant is sufficient. |
| 3 | Premature abstraction | pass | The new curvature feature/segmentation surfaces are plain result/config records and free functions. Their exports have production consumers in runtime, simplification, or benchmark paths plus focused tests. No new one-implementation interface, factory, bridge, registry, or service survives without a present boundary or consumer. |
| 4 | Documented-but-not-tested | pass | Forty-two of 49 production-source commits carry tests in the same commit. The seven remaining candidates were inspected individually: they are source README/comment corrections, evidence-adjacent edits, or refactors covered by adjacent task slices and current suites. No sampled behavioral documentation claim lacks an executable proof. |
| 5 | Defensive validation at internal boundaries | pass | A lexical ranking found 341 added negative/null-guard candidates. Samples from the densest curvature and runtime files guard numerical preconditions, optional property lookups, external asset/config input, stale async apply state, or backend failure boundaries. No reference-initialized dead-guard family or duplicated internal validator was found. |
| 6 | Untracked compatibility shims | pass | The current-tree marker sweep found no unowned temporary compatibility branch. Remaining temporary wording describes scoped scratch/state objects. The tested `progressiveRenderData` read alias is the permanent compatibility spelling owned and accepted by retired `RUNTIME-193`, not an expiring migration exception. |
| 7 | Ceremony without shipped value | pass | The window contains substantial geometry, runtime, editor, renderer, importer, test, and reliability delivery alongside its task/evidence records. The removed `src_new` experiment is visible churn, but it was fully retired rather than left as a second tree or abstraction surface. |
| 8 | Half-finished implementations | pass | Every newly added module surface was checked for a production consumer or end-to-end/focused test. Curvature segmentation/features are wired through runtime publication and visualization, and the other public additions have concrete consumers. No test-only public scaffold, self-only module, or placeholder backend remains. |
| 9 | Aspirational documentation without `(planned)` marker | findings | New `src/runtime/README.md` prose mixes current contracts with task chronology and future ownership: for example, “BUG-140 established ... BUG-145 is extending”, “BUG-163 also exposes”, and “GEOM-076 owns ... follow-up”. Other new paragraphs explain what callers “used to” do. The behavior described is real, but the README should state the current rule first and link to task history only when provenance is still useful. |

## Follow-ups

- Rows 2 and 9 are owned by
  [`HARDEN-089`](../../tasks/active/HARDEN-089-audit-window-source-documentation-cleanup.md).
  That standard-profile cleanup is deliberately bounded to this audit window:
  five missing interface synopses, six identified historical comment blocks,
  current-state runtime README wording, and review of the 98 added source lines
  that mention a task ID. It forbids executable C++ or public-surface changes.
- `tasks/HINTS.md` contains no unresolved hints, so no hints-ledger follow-up
  is required.

## Evidence limits

- Commit/path classification is exhaustive; semantic review is risk-ranked
  sampling as required by the checklist.
- Source-documentation review findings are prompts, not automatic violations.
  Only the five missing synopses are objective scanner failures; the historical
  comments and README passages were manually confirmed before opening the
  bounded cleanup.
- This audit records source quality and task hygiene. CPU, sanitizer, and
  Vulkan executions remain separate evidence classes owned by the integrated
  bug PRs; the audit makes no new performance or capability claim.

## Elapsed time

Approximately 25 minutes. Complete-range extraction and rankings were
automated; manual review stayed within the checklist's 60-minute budget.
