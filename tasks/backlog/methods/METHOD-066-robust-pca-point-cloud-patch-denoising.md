---
id: METHOD-066
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: proposal filed from REVIEW-007 E7; implementation follows the method workflow (paper intake, CPU reference, analytic tests, benchmark) and owes research evidence before any claim.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation]
---
# METHOD-066 — RobustPCA point-cloud patch denoising and outlier scoring

## Goal
- Per sample, stack its centred kNN patch as a k x 3 matrix. **Low-rank L:** the locally planar
  surface (rank <= 2). **Sparse S:** outlier coordinates and spikes. Publish an outlier score
  (row norm of S), an optional denoised position (L row + centroid) and an optional normal
  (smallest singular direction of L).
- Optional second formulation, decided at intake: non-local groups of similar height-field patches
  (patches as columns).
- Origin: REVIEW-007 E7 (2026-10-06), GE15 — application of `Geometry::Linalg::RobustPCA`
  (Principal Component Pursuit, L + S by ADMM; stopping rule fixed in `74135dfff`). This is a
  **proposal**; any quality or performance statement needs an `ara/logic/claims.md` row first
  (AGENTS.md §8b).

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Finite `vec3` samples from a selected property plus a kNN neighbourhood over them; no normals or topology needed. |
| Compatible entity sources | Any compatible `vec3` `GeometryPropertyRef` binding, independent of property name: mesh vertices/halfedges/edges/faces, graph nodes/halfedges/edges, point-cloud points (samples-only kernel). |
| RuntimeModule | Existing `Runtime.PointAnalysisOperations`/OutlierAnalysis family: new `OutlierAnalysisMethod::RobustPCA` reusing Analyze/RemoveMarked; denoised positions/normals published through the existing point-field publication. |
| Config/agent | `sandbox.outlier_analysis` gains `robust_pca` with `k`, `lambda` (0 = default), `tolerance`, `outputs`; existing outlier agent operation. |
| UI | Outlier-analysis panel method entry; parameters shown only for this method. |
| Publication | On the source element domain: scalar score and Bool mask; optional `vec3` denoised-position and normal properties; positions are replaced only on an explicit apply with one undo step. |
| End-to-end tests | Every compatible domain listed above (incl. faces, edges, halfedges) → config/panel/agent → score/mask/positions, revision, undo, save/load. |

## Acceptance criteria
- [ ] Paper intake recorded (formulation, lambda choice for k x 3, entrywise vs l2,1 sparsity);
      literature to verify: Candès–Li–Ma–Wright 2011; Xu–Caramanis–Sanghavi (outlier pursuit);
      robust local PCA surface methods.
- [ ] Kernel: if whole-point outliers need it, a column/row-sparse (l2,1) variant is added to
      `Geometry.Linalg` with CPU tests; otherwise the reason is recorded.
- [ ] CPU reference passes analytic fixtures: noisy plane with injected outliers (scores separate,
      normals recover the plane), sphere patch, coincident/degenerate neighbourhoods fail closed.
- [ ] Comparison against the existing Statistical/Radius/LocalDistanceRatio methods and PCA normals
      on a declared fixture under the benchmark workflow; no adoption or default change without it.
- [ ] Panel and agent share config/readiness/apply; methods/docs describe limits (cost per sample,
      k choice, thin features).

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(LinearAlgebra|OutlierAnalysis|OutlierAnalysisConfig|OutlierAnalysisOperations|AgentOperations|SandboxConfigSections|SandboxProcessingPanels|RuntimeEngineLayering|RuntimeEnginePrivateGlue)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```

## Context
- Neighbours: GEOM-094 (Vulkan outlier reductions), GEOM-095 (bilateral point filter), GEOM-104
  (point PCA features), GEOM-114 (PCA isotropic cutoff). Not absorbed.
- RobustPCA computes a full SVD per ADMM iteration; on k x 3 patches this is cheap, but total cost
  scales with sample count x iterations — measure before any GPU idea.
