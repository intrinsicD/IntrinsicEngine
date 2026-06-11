# Agent-output Weekly Audit — 2026-05-28

Third cadence audit against
[`docs/agent/agent-output-review-checklist.md`](../agent/agent-output-review-checklist.md).
Follows the [2026-05-26 audit](2026-05-26-agent-output-audit.md); this is the
regular calendar-cadence sweep covering the slices landed since that audit's
window closed.

## Window

- Commit range: `b57dd36..HEAD` (the head of `claude/intelligent-wright-QpRVo`).
- Date range: 2026-05-26 → 2026-05-28 (28 non-merge commits).
- Tasks covered (11):
  [`GEOM-008`](../../tasks/done/GEOM-008-linear-algebra-solver-infrastructure.md)
  (retired; Slice A linear-algebra + sparse-solver infrastructure),
  [`GEOM-012`](../../tasks/done/GEOM-012-symmetric-domain-views-property-sharing.md)
  (active at the time of this audit; Slices A + B landed — `Geometry.DomainViews`;
  retired to `tasks/done/` on 2026-05-29 after Slice E),
  [`GEOM-016`](../../tasks/backlog/geometry/GEOM-016-point-cloud-filtering-density-contracts.md)
  (backlog filing + LDLT follow-up gating METHOD-002/003),
  [`RUNTIME-082`](../../tasks/done/RUNTIME-082-spatial-debug-adapters.md)
  (active; Slice C `ConvexHullAdapter` + registry, Slice D extraction pump),
  [`RUNTIME-085`](../../tasks/done/RUNTIME-085-geometrysources-mesh-residency.md)
  (active; Slices A–C — `Runtime.MeshGeometryPacker` + residency),
  [`GRAPHICS-076`](../../tasks/done/GRAPHICS-076-default-recipe-debug-view-and-present-wiring.md)
  (retired; default-recipe Vulkan smoke graduated),
  [`GRAPHICS-077`](../../tasks/done/GRAPHICS-077-transient-debug-primitive-upload-helper.md)
  /
  [`GRAPHICS-078`](../../tasks/done/GRAPHICS-078-visualization-overlay-upload-helper.md)
  (overlay Vulkan smokes + retirement commit refs),
  [`HARDEN-071`](../../tasks/done/HARDEN-071-rhi-manager-handle-identity.md)
  (retired; RHI buffer/texture manager handle identity),
  [`HARDEN-072`](../../tasks/done/HARDEN-072-rhi-surface-fixes-for-default-recipe-pipeline-bringup.md)
  (retired; push-constant limit + virtual default-arg fix),
  INFRA Option A (seal hot dependency cache; backlog
  [`INFRA-001`](../../tasks/active/INFRA-001-vcpkg-manifest-mode.md)
  tracks the follow-on vcpkg move).
- Sampling: rows 1–9 spot-checked against representative substantive commits
  per task family, with heavy weight on the new module surfaces introduced this
  window: `Geometry.DomainViews`, `Geometry.Linalg`, `Geometry.Sparse`,
  `Runtime.MeshGeometryPacker`, `ConvexHullAdapter` (the fourth
  `ISpatialDebugAdapter` concrete), and `ECS.Component.SpatialDebugBinding`.

Notable substantive commits used as evidence anchors:

- `c1aeafb` GEOM-008 Slice A — Eigen3 dependency, `Geometry.Sparse`/`Geometry.Linalg`, DEC bridge.
- `a2b8eca` + `408a3cd` GEOM-012 Slices A/B — `Geometry.DomainViews` mesh-as-graph / mesh-as-cloud borrows.
- `81b523e` + `8908afe` + `63e2009` RUNTIME-085 Slices A–C — mesh `GeometrySources` packer, extraction wiring, dirty-domain reupload.
- `2697f86` RUNTIME-082 Slice C — `ConvexHullAdapter` + adapter registry.
- `215d5f8` + `9294102` RUNTIME-082 Slice D — wire spatial-debug adapter pump through extraction.
- `070e586` HARDEN-071 — RHI buffer/texture manager handle identity.
- `c7d5a76` HARDEN-072 — push-constant limit bump + virtual default-arg removal.
- `d5d059a` INFRA Option A — seal hot dependency cache; stop auto-deleting it.

## Findings

| Row | Failure mode | Outcome | Evidence |
| --- | --- | --- | --- |
| 1 | Silent scope creep | pass | `git show --stat` on the sampled commits keeps changes inside the named task's scope. `c1aeafb` (GEOM-008) is bounded to the two new geometry modules, the DEC alias bridge, their two unit tests, the geometry architecture docs, the Eigen3 dependency wiring, and the task files. `a2b8eca`/`408a3cd` (GEOM-012) touch only `Geometry.DomainViews.{cppm,cpp}`, `Geometry.PointCloud`, the borrow test, and geometry docs. The broad `assets/shaders/*` touches (`ddf5534`, `cc06ede`) map to GRAPHICS-076 Slice D command-stream bring-up and the GRAPHICS-076E readback seam respectively — both within declared task scope, not drive-by edits. `070e586` (HARDEN-071) stays inside `src/graphics/rhi/`. `d5d059a` (INFRA) touches only `cmake/Dependencies.cmake` + `tools/setup/populate_deps.sh`. No cross-layer drive-by edits found. |
| 2 | Decorative comments and docstrings | pass | New comment blocks encode WHY/contract, not WHAT. `Geometry.DomainViews.cppm` documents lifetime guarantees, mutation semantics on face-bearing meshes, and property-set sharing — invariants not inferable from the signature. `Runtime.MeshGeometryPacker.cppm` explains why `MeshVertex` mirrors `ProceduralVertex` (pipeline reuse) and why deleted-face detection keys on `h:face` ownership rather than `f:halfedge`. `ConvexHullAdapter` (`Runtime.SpatialDebugAdapters.cppm:109-117`) documents why shared-plane incidence is sufficient for polytope edges. Data-only types in `Geometry.Sparse`/`Geometry.Linalg` rely on self-documenting names with minimal commentary. No multi-paragraph docstrings restating parameters on internal helpers. |
| 3 | Premature abstraction | pass | `ISpatialDebugAdapter` now carries four concrete implementations in-tree (`BvhAdapter`, `KdTreeAdapter`, `OctreeAdapter`, and this window's `ConvexHullAdapter`) plus the registry seam consumed by extraction — a real seam, well past the two-adapter bar. `Geometry.Sparse` and `Geometry.Linalg` are free-function numeric modules, not wrapper layers; `Geometry.DomainViews` exposes two borrow free-functions, no factory/builder ceremony. No new single-implementation `I*` interfaces or `Make*`/`Build*` helpers introduced this window. |
| 4 | Documented-but-not-tested | pass | Behavioral doc claims carry matching tests. `geometry.md`'s `BorrowMeshAsGraphReadOnly`/`BorrowMeshAsCloud` property-sharing claims are pinned by `Test.SubmeshViewDomainBorrows.cpp` (Slices A+B) and exercised in `Test_ShortestPath.cpp`. `geometry-api-style.md`'s hybrid GLM+Eigen3 policy is backed by `Test.LinearAlgebra.cpp` + `Test.Sparse.cpp` (GEOM-008). The two new roadmap docs (`parameterization-mapping-roadmap.md`, `point-cloud-algorithm-roadmap.md`) open with `Status: roadmap / planning note` and explicitly disclaim implementation — correctly out of scope for this row. |
| 5 | Defensive validation at internal boundaries | findings | `ConvexHullAdapter` (landed this window in `2697f86`) repeats the HARDEN-070 dead-guard pattern: the constructor binds `m_Hull` from a `const Geometry::ConvexHull&` parameter with the rvalue overload deleted (`Runtime.SpatialDebugAdapters.cppm:121,126`), yet `ConvexHullAdapter::Append` opens with an `if (m_Hull == nullptr) return;` early-return (`Runtime.SpatialDebugAdapters.cpp:391`) that the type system makes unreachable in well-formed code. This is the fourth adapter in the same module HARDEN-070 already targets; HARDEN-070 named `ConvexHullAdapter` as then-"planned" in its Non-goals but excluded it because it had not yet landed. No new dead guards elsewhere: the `view.VertexSource/HalfedgeSource/FaceSource == nullptr` checks in `Runtime.MeshGeometryPacker.cpp` validate a caller-supplied `GeometrySources::ConstSourceView` (external input — legitimate), and the registry-lookup `adapter == nullptr` check in `Runtime.RenderExtraction.cpp` validates an untrusted map lookup (legitimate). |
| 6 | Untracked compatibility shims | pass | The DEC `using SparseMatrix = Geometry::Sparse::SparseMatrix;` (and `DiagonalMatrix`/`CGParams`/`CGResult`/`CGConvergenceReason`) aliases in `Geometry.HalfedgeMesh.DEC.cppm` are permanent compatibility bridges documented as such in `geometry.md` — not temporary shims, so no removal task is owed. The temporary promoted-Vulkan sampled-present sampler bridge in `Backends.Vulkan.{Mappings,Device}.cpp` carries an explicit `GRAPHICS-076E` task reference, and the GEOM-011 fallback-solver note defers to `GEOM-020`. No untracked `TODO`/`FIXME`/`deprecated`/`backcompat` source additions found in the window. |
| 7 | Ceremony without shipped value | pass | 28 commits; the clear majority are substantive code-and-test slices — `Geometry.Sparse`/`Geometry.Linalg` (GEOM-008), `Geometry.DomainViews` (GEOM-012), `Runtime.MeshGeometryPacker` + residency (RUNTIME-085), `ConvexHullAdapter` + extraction pump (RUNTIME-082), the RHI manager handle-identity refactor (HARDEN-071), and the push-constant/virtual-arg fixes (HARDEN-072). The INFRA Option A cache-seal commit (`d5d059a`) is real infrastructure value (configure time ~240 s → ~0.4 s on a hot cache). Substantive commits retired four tasks (GEOM-008, GRAPHICS-076, HARDEN-071, HARDEN-072) and advanced three active tasks through real slices. The docs-only / task-only commits (roadmap retirements, backlog filings) are proportional per-slice ceremony, not a whole-window stall. |
| 8 | Half-finished implementations | pass | Every new public symbol has a non-test call site or an end-to-end test. `Geometry.Sparse` is consumed by `Geometry.HalfedgeMesh.DEC` (and through it Geodesic/Parameterization/Smoothing) and unit-tested. `Geometry.DomainViews` is exercised by `Test.SubmeshViewDomainBorrows.cpp` and `Test_ShortestPath.cpp`. `Runtime.MeshGeometryPacker::PackMesh` is wired into `Runtime.RenderExtraction.cpp` (`BindMeshGeometry`) and covered by contract + integration tests. `ConvexHullAdapter` and the adapter registry are registered via `RenderExtractionCache::RegisterSpatialDebugAdapter` and resolved in `ExtractAndSubmit`, with integration tests. `ECS.Component.SpatialDebugBinding` is walked in the extraction loop and tested end-to-end. See the calibration note on `Geometry.Linalg`. |
| 9 | Aspirational documentation without `(planned)` marker | pass | Present-tense architecture assertions match landed code: `geometry.md`'s borrow-view claims and `geometry-api-style.md`'s hybrid-numeric policy both correspond to symbols that exist on the working tree, and the deferred Slice C/D borrow variants are explicitly marked as deferred. The two new roadmap docs are explicitly labeled `Status: roadmap / planning note` with future task IDs. `src/runtime/README.md` (RUNTIME-085) and `src/graphics/*/README.md` updates describe landed behavior or carry the GRAPHICS-076E backlog reference for the not-yet-complete readback. No unmarked future-state claims found. |

## Follow-ups

- **Row 5 (defensive internal-boundary null checks).** Folded into the existing
  [`HARDEN-070`](../../tasks/done/HARDEN-070-drop-dead-null-guards-on-reference-initialised-helpers.md)
  cleanup. `ConvexHullAdapter::Append`'s `m_Hull == nullptr` guard
  (`Runtime.SpatialDebugAdapters.cpp:391`) is the same dead-guard pattern on a
  sibling adapter in the *same file* HARDEN-070 already edits, and HARDEN-070
  named `ConvexHullAdapter` by name as then-"planned". Rather than fragment a
  one-line mechanical removal into a separate task (which would itself be Row-7
  ceremony), HARDEN-070's scope was extended to the fourth adapter and its
  `m_Hull` guard. HARDEN-070 had not yet been worked, so amending its
  required-changes before any slice starts is cheap and keeps the identical
  cleanup in one place.

  Until `HARDEN-070` lands, no per-PR action is required — the guard does not
  change observable behavior; the audit records the recurrence so the next
  slice on `Runtime.SpatialDebugAdapters` does not propagate it further.

## Calibration note

- **`Geometry.Linalg` has no engine consumer yet (not a Row-8 finding).** All
  six `Geometry.Linalg` exports (`ComputeSVD`, `ComputeQR`,
  `ComputeSymmetricEigen`, `ComputePolarDecomposition`, `SolveLeastSquares`,
  `CovarianceAccumulator`) are exercised end-to-end by `Test.LinearAlgebra.cpp`
  but have no non-test call site yet. Row 8 passes on the OR-criterion (a new
  symbol needs a non-test call site *or* an end-to-end test, and the tests
  exist). This is intentional infrastructure-ahead-of-consumers: GEOM-008's
  `Why` explicitly delivers the CPU numerical-infrastructure gap and defers the
  consuming algorithm rewrites; the consumers are tracked
  ([`GEOM-020`](../../tasks/backlog/geometry/GEOM-020-sparse-direct-factorization-seam.md),
  METHOD-002/003 gated on GEOM-016). Flagged here so the next audit does not
  re-open it as drift, and so the next reviewer re-checks that at least one of
  those consumers has begun wiring `Geometry.Linalg` before the gap ages
  further.

The window (28 commits) was walked per-task-family from `git show --stat`,
`git diff b57dd36..HEAD` greps, and targeted reads of the six new module
surfaces. Every row was decidable without a full build. Row 5 produced the only
finding, a low-risk mechanical recurrence already covered by an open task.

## Elapsed time

- Start: 2026-05-28T18:26Z.
- Finish: 2026-05-28T18:33Z.
- Total: ≈ 7 minutes for the audit sweep plus the HARDEN-070 scope amendment and
  this report — well under the 60-minute target. The parallel evidence-gathering
  across the nine rows (premature-abstraction/half-finished, decorative-comments/
  defensive-validation, and scope-creep/docs clusters) kept the sweep inside the
  calibration budget despite the new module surfaces.
