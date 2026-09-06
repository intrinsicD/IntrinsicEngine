---
id: METHOD-041
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive CPU-reference and visual-inspection slice; executable tests and local artifacts, with no adoption or performance claim."
owner: codex
branch: codex/method-040-boundary-partition
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-06T23:26:45+02:00"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration, repo.source-documentation]
contract_review: "A mesh-only geometric curve detector needs embedded triangles; outputs are detached curves without input mutation. Runtime publication is a separately tracked integration step."
---
# METHOD-041 — Inspect explicit curvature-extremum curves

## Goal
- Extract and inspect explicit curvature-extremum curves before changing METHOD-040 partition selection or merging.

## Context
- The operator explicitly requested this experiment with Claude after inspecting METHOD-040: clean models look good, while frog should follow dominant curvature creases more closely. Mean curvature is a candidate signal; L0 geometry optimization is a comparison intuition, not mandatory preprocessing.
- This is the agreed first slice: CPU reference, analytic verification and an interactive overlay on frog, sculpt and trim-star. Existing METHOD-040 remains available for comparison.

## Design
- Read Hildebrandt–Polthier–Wardetzky (2005), especially directional extremality, consistent line-field signs and regular triangles. Use a normal-variation symmetric shape-operator fit to obtain geometric principal directions at explicit geodesic support radii without moving vertices.
- Compare principal ridge/valley zero curves against mean-curvature scalar ridges/valleys using Hessian directions. Preserve subtriangle crossings; distinguish exact sharp mesh edges from smooth extrema.
- Expose strength, fit residual, directional reliability and cross-scale agreement as diagnostics, without treating heuristic confidence as a calibrated probability.
- Bound neighborhood work and return explicit failure without partial output. Keep physical radius, input identity and active thresholds in inspection outputs.
- Right sizing: one geometry module with plain parameter/result records, native runner and extension of the existing offline viewer. No service, registry, new renderer or automatic partition adoption.

## Engine integration
| Field | Decision |
| --- | --- |
| Least-structured input | Embedded triangle mesh with finite positions and surface adjacency; point sets alone do not meet the input contract. |
| Compatible entity sources | Any owning mesh satisfying the same topology/conditioning preflight; no OBJ-only kernel assumption. |
| RuntimeModule | UI-053 owns engine-side detached extraction and publication of these curve results. |
| Config/agent | Explicit serializable runner parameters now; UI-053 owns shared engine config preview/apply integration. |
| UI | Standalone interactive exact-curve overlay now; UI-053 owns Sandbox controls. |
| Publication | Detached float-position curve graph with source face/edge interpolation provenance; input properties and topology remain intact. UI-053 owns engine publication. |
| End-to-end tests | Analytic CPU extraction tests and native export/viewer validation now; UI-053 owns engine/config/history tests. |

## Acceptance criteria
- [x] Freeze cited equations, parameters, diagnostics, numerical limitations and scope.
- [x] Implement a deterministic CPU reference with correct direction/sign handling, shared edge crossings and bounded work.
- [x] Test known extrema, homogeneous negatives, orientation reversal, scale/translation, remeshing, invalid input and work limits.
- [x] Provide exact curve overlays with scale/signal/strength controls and METHOD-040 comparison where available.
- [x] Run on frog, sculpt and trim-star plus available varied meshes; retain failures and avoid claims based only on curve count.
- [x] Review formulation with Claude, verify source locally, and pass relevant structural/build/test gates.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryCurvatureExtremaTests IntrinsicGeometryFeaturePartitionTests IntrinsicCurvatureExtremaMesh
ctest --test-dir build/ci --output-on-failure -R CurvatureExtrema --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_method_manifests.py
python3 tools/docs/check_doc_links.py --root . --strict
```

## Result
CPUContracted detector and standalone inspector. See the [inspection record](../../methods/geometry/curvature_segmentation/curvature_extrema_report.md) and [evidence](../evidence/METHOD-041). UI-053 owns deferred engine integration; partition adoption remains an explicit later experiment.
