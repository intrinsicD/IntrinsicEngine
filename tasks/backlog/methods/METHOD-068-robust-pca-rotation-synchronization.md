---
id: METHOD-068
theme: I
depends_on: [RUNTIME-322]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: proposal filed from REVIEW-007 E7; implementation follows the method workflow (paper intake, CPU reference, analytic tests, benchmark) and owes research evidence before any claim.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources, repo.source-documentation]
---
# METHOD-068 — RobustPCA rotation synchronization for multi-view alignment

## Goal
- Given relative rotations R_ij between N views (from pairwise registrations), recover absolute
  rotations R_i. The 3N x 3N block matrix G with blocks R_i R_j^T is **low-rank (rank 3)**;
  wrong pairwise registrations are **sparse** corrupted blocks; unregistered pairs are missing.
  Blocks of the recovered L are projected to SO(3); `Geometry.RotationAveraging`
  (e.g. `GeodesicMedian`, `ChordalMean`) is the per-view refinement and the baseline.
- Product use: global alignment of several scans of one object after pairwise ICP, the
  multi-view case deliberately left out of [RUNTIME-322](../runtime/RUNTIME-322-rotation-averaging-editor-agent-integration.md).
- Origin: REVIEW-007 E7 (2026-10-06), GE15 (with GE06) — application of
  `Geometry::Linalg::RobustPCA` (Principal Component Pursuit, L + S by ADMM; stopping rule fixed in
  `74135dfff`). This is a **proposal**; any accuracy statement needs an `ara/logic/claims.md` row first
  (AGENTS.md §8b).

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | N entities and a set of relative rotations R_ij (any subset of pairs) with optional confidences. |
| Compatible entity sources | Mesh vertices, point-cloud points, graph nodes as registration inputs; the result applies to entity TRS. |
| RuntimeModule | New `Runtime.RotationSynchronizationOperations`; pairwise R_ij come from the existing `Runtime.RegistrationOperations` (ICP) run over selected pairs inside the operation (bounded N); no persistent view-graph store. |
| Config/agent | `sandbox.rotation_synchronization`: pair policy (all/neighbours/explicit), lambda, tolerance, refinement method (the RUNTIME-322 enum), anchor view; `preview_operation`/`run_operation`. |
| UI | Panel over the multi-selection: pair list with per-pair residual and flagged-outlier state, anchor choice. |
| Publication | N entity rotations as one undo transaction through the RUNTIME-322 transform path; translations unchanged (translation synchronization is a follow-up). |
| End-to-end tests | Synthetic views with known rotations and corrupted pairs → panel/agent → rotations within tolerance, flagged pairs reported, single undo. |

## Acceptance criteria
- [ ] Kernel extension in `Geometry.Linalg` (first slice, CPU tests; reuse if another RobustPCA task
      already added it): observation mask — `RobustPCAOptions` has no missing-entry set, and
      unregistered pairs must not be treated as zero blocks.
- [ ] Paper intake recorded; literature to verify: Wang–Singer (LUD), Chatterjee–Govindu (robust
      rotation averaging), Arrigoni et al. (low-rank + sparse rotation synchronization).
- [ ] CPU reference recovers rotations up to a global gauge (anchor) on synthetic graphs with a
      stated corruption rate; disconnected view graphs and all-corrupt inputs fail closed.
- [ ] Baseline comparison against RotationAveraging-based IRLS on the same graphs under the
      benchmark workflow; no default/adoption without it.
- [ ] Panel and agent share config/apply; docs state the gauge, the ICP dependency and limits.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(LinearAlgebra|GeometryRotationAveraging|RotationSynchronizationOperations|RotationSynchronizationConfig|RotationAveragingOperations|AgentOperations|SandboxConfigSections|RuntimeEngineLayering|RuntimeEnginePrivateGlue)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```

## Context
- `docs/architecture/geometry-pipeline-modularity.md` names RotationAveraging as the substrate for a
  future global stage; this task is one concrete form of it.
- METHOD-073 (structure from motion) and METHOD-074 (map synchronization) share the masked block-matrix
  formulation; reuse the mask extension.
