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

- [ ] Arbitrary compatible scalar vertex/face properties guide deterministic
  segmentation; disconnected components and region boundaries survive charting.
- [ ] Atlas generation validates inputs and supports bounded, truthful None,
  Angle, Area, and Both objectives with explicit requested/actual/fallback reports.
  Invalid geometry, resource limits, failed quality gates, and under-resolution
  produce actionable diagnostics instead of successful-looking corrupt UVs.
- [ ] Published corner UVs cover every accepted source triangle without changing
  source topology or unrelated properties. Finite bounds, orientation, chart and
  triangle overlap, region preservation, density and distortion are audited.
- [ ] Config, commands and UI share validated bindings/settings and preserve
  cancellation, stale-job handling, undo, and scene/config round trips.
- [ ] Baked properties share the selected atlas revision. Raw values, coverage,
  padding and stale-output handling are correct and independently tested.
- [ ] Successful atlas generation opens a resizable left/right split showing the
  selected mesh and its UV wireframe. The atlas and each baked texture have tabs,
  shared navigation, useful legends, and honest pending/failed/stale states.
  Camera aspect, rendering, picking and gizmos use the actual scene rectangle.
- [ ] Focused CPU tests, analytical/corpus quality cases, full CPU, separate
  ASan/UBSan, and relevant Vulkan/readback/UI paths pass. Benchmark evidence is
  recorded without unsupported universal reliability or speed claims.
- [ ] Claude reviews fixed implementation revisions, findings are resolved,
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
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-asan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
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
