---
id: METHOD-047
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive implementation; evidence is the diff, focused and full verification, and revision-bound Claude reviews
contract_schema: 1
contracts:
  - geometry.element-domain-sources
  - geometry.property-coherence
  - geometry.parameterization-optimization
  - method.engine-integration
  - runtime.texture-bake-interface-locality
  - runtime.processing-compilation-locality
  - runtime.editor-prepared-frame-locality
---
# METHOD-047 — Property-guided UV atlas and texture inspection

## Goal

Implement the operator-requested scalar-guided mesh segmentation, validated UV
atlas, property baking, and actual mesh/atlas split-view workflow. Operator
direction on 2026-09-22 explicitly selects this work ahead of the standing P0.
Claude Opus 5.5 participates in implementation and revision-bound reviews;
Codex owns integration and final verification. The historical design discussion
is [preserved as evidence](../../ara/evidence/diagnostics/property_guided_atlas_design_20260922/proposal.md).

## Acceptance criteria

- [x] Arbitrary compatible scalar vertex/face properties guide deterministic
  segmentation; disconnected components and region boundaries survive charting.
- [x] Atlas generation validates inputs and supports bounded, truthful None,
  Angle, Area, and Both objectives with explicit requested/actual/fallback reports.
  Invalid geometry, resource limits, failed quality gates, and under-resolution
  produce actionable diagnostics instead of successful-looking corrupt UVs.
- [x] Published corner UVs cover every accepted source triangle without changing
  source topology or unrelated properties. Finite bounds, orientation, chart and
  triangle overlap, region preservation, density and distortion are audited.
- [x] Config, commands and UI share validated bindings/settings and preserve
  cancellation, stale-job handling, undo, and scene/config round trips.
- [x] Baked properties share the selected atlas revision. Raw values, coverage,
  padding and stale-output handling are correct and independently tested.
- [x] Successful atlas generation opens a resizable left/right split showing the
  selected mesh and its UV wireframe. The atlas and each baked texture have tabs,
  shared navigation, useful legends, and honest pending/failed/stale states.
  Camera aspect, rendering, picking and gizmos use the actual scene rectangle.
- [x] Focused CPU tests, analytical/corpus quality cases, full CPU, separate
  ASan/UBSan, and relevant Vulkan/readback/UI paths pass. Benchmark evidence is
  recorded without unsupported universal reliability or speed claims.
- [x] Claude reviews fixed implementation revisions, findings are resolved,
  current documentation is synchronized, and remaining limitations are explicit.

## Engine integration

| Field | Decision |
| --- | --- |
| Least-structured input | Indexed triangle surface, finite typed positions, optional scalar vertex/face property; rejected degeneracies and unsupported topology are explicit. |
| Compatible entity sources | Every mesh entity with the required surface topology and typed properties, independent of importer/provenance. |
| RuntimeModule | Existing parameterization/UV regeneration, curvature-segmentation and texture-bake operations and shared readiness paths. |
| Config/agent | Persisted parameterization/atlas controls and typed property references; UI and commands use the same validated operations. |
| UI | Mesh processing controls and selected-mesh/UV split; atlas and generated-texture tabs. |
| Publication | Source corner UV property, preserved mesh/cardinalities and unrelated properties; existing job/history/undo mechanisms; atlas-bound bake records. |
| End-to-end tests | Runtime contract tests from constructed and imported compatible meshes, property binding, UV publication, bake freshness, view-model/UI and GPU readback tests. |

## Slice plan and reuse

1. Geometry: reuse `Geometry.UvAtlas`, typed outcomes, feature segmentation,
   parameterization solvers/optimization kernels and xatlas packing. Extend
   present APIs; do not introduce a generic segmentation/viewport framework.
2. Runtime: reuse geometry-property preflight, asynchronous UV jobs, source-corner
   recovery and history. Carry explicit source/atlas identity into bake records.
3. Editor: extend the existing derived UV view, authoritative scene rectangle,
   parameterization controls and generated asset display. No duplicate ECS mesh.
4. Validate and refine: CPU raster oracle, analytic/corpus cases, focused Vulkan
   smoke, fixed-diff Claude review, required complete gates and documentation.

The existing seed-plane charting has no region constraints; existing optimization
kernels have no complete selectable atlas objective loop; the view model currently
rejects corner UVs. These concrete contract mismatches justify extending those
owners. Offline METHOD-040/044/045/046 experiments remain frozen evidence, not
automatically adopted algorithms. No new external dependency is planned.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j2
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests -j2
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicCpuTests -j2
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests -j2
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 120
python3 tools/agents/check_task_policy.py --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
git diff --check
```

## Log

- 2026-09-22: Implementation authorized by the operator, including repeated Claude
  Opus 5.5 implementation/review/test/fix iterations. High effort is the default;
  deeper effort is reserved for numerical correctness and difficult findings.
  Simultaneous writers use separate worktrees and separate build directories.
- 2026-09-23: Resumed after a system-wide forced logout. Saved each implementation
  worktree diff before integration. Builds are serialized under a shared lock,
  with at most two compiler jobs and a 24 GiB process-group memory ceiling; one
  Claude process runs at a time. Geometry revision `2e726cbb4` replaces the
  interrupted snapshot. Combined verification and an independent max-effort
  Claude review of source snapshot `947438ca9` are in progress; acceptance remains
  open until those gates and any required fixes complete.
- Review-1 fixes retain strict atlas admission while making automatic import UV
  resolution optional: otherwise a small under-resolved island discards an
  otherwise usable model. Explicit required-UV materialization remains strict.
  This is a scoped correction for the operator's robust-mesh workflow, with
  direct/model import and render-extraction regressions; the import visibility
  checklist now describes this actual failure path. Polygon atlas publication
  rejects non-triangular source faces before queueing to preserve topology and
  avoid last-write-wins corruption of shared source corners.


## Completion

PR/commit: [PR #1044](https://github.com/intrinsicD/IntrinsicEngine/pull/1044), implementation `9221c02c8`.

Retired 2026-09-23 at Operational for the bounded triangle-mesh CPU atlas and
recorded Vulkan bake/editor path. The enclosing METHOD-047 implementation commit
records retirement. All acceptance criteria are closed; implementation, five
fixed-source Opus 5.5 reviews, a numerical audit and corrections are retained in
the [evidence report](../../ara/evidence/diagnostics/method047_property_atlas/report.md).
The final nested-history correction follows the last source review's prescribed
fix and passes the extended undo/redo regression. BUG-209 records unchanged-source
recovery of an outdated incremental Vulkan module view.

C109 supports only the retained finite CPU cohort; C110 supports the executed
GPU path and named readback checks. This is not a universal-mesh, semantic-part,
optimizer-convergence or speed guarantee. Invalid and under-resolved inputs fail
explicitly. Triangle-only runtime publication, CPU-only solvers, exact accepted
atlas extents, the 8192 bake limit and session-only bake texels are documented
boundaries, not hidden fallbacks. No in-scope implementation work is deferred.

Builds and numerical runs were serialized with a two-compiler-job maximum,
16 GiB memory high watermark, 24 GiB maximum and 1 GiB swap maximum. Sanitizer
CTest remained serial. The final rendered split workspace was visually inspected
in addition to automated GPU/scene-rectangle assertions.
