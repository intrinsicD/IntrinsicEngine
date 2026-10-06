---
id: METHOD-072
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
# METHOD-072 — RobustPCA cleanup of animation and motion-capture trajectories

## Goal
- Marker or joint trajectories as an F x 3M matrix (frames x marker coordinates): correlated body
  motion is **low-rank**; marker swaps, spikes and ghost markers are **sparse**; occlusion gaps are
  **missing** entries. Outputs: cleaned and gap-filled trajectories plus a per-sample corruption flag.
- Origin: REVIEW-007 E7 (2026-10-06), GE15 — application of `Geometry::Linalg::RobustPCA`
  (Principal Component Pursuit, L + S by ADMM; stopping rule fixed in `74135dfff`). This is a
  **proposal**; any cleanup-quality statement needs an `ara/logic/claims.md` row first (AGENTS.md §8b).

## Missing infrastructure (prerequisite for real mocap data, not in scope)
- The engine has no mocap import (C3D/TRC/BVH), skeleton or animation data. Without it, this task
  works on the engine-native equivalent: an ordered selection of point-cloud (or graph) entities with
  equal point count and index correspondence, one entity per frame. Real mocap files need a separate
  import task filed first; skeleton/joint-angle cleanup needs animation infrastructure and is out of scope.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Ordered F frames of M `vec3` samples with index correspondence; non-finite or flagged samples are missing. |
| Compatible entity sources | Any compatible `vec3` `GeometryPropertyRef` binding, independent of property name: mesh vertices/halfedges/edges/faces, graph nodes/halfedges/edges, point-cloud points; all frames use the same domain and cardinality. |
| RuntimeModule | Shares the multi-entity sequence capture with METHOD-067 (whichever lands first owns it); new trajectory-cleanup operation on it. |
| Config/agent | `sandbox.trajectory_cleanup`: frame order, lambda, tolerance, missing-value policy; `preview_operation`/`run_operation`. |
| UI | Panel over the multi-selection: corrupted/missing counts per marker, frame scrubber. |
| Publication | On each frame's source element domain: cleaned `vec3` values written back to the bound property (one undo step for the sequence) and a Bool corruption flag property. |
| End-to-end tests | Every compatible domain (incl. faces, edges, halfedges) plus synthetic articulated motion with injected swaps/spikes/gaps → panel/agent → error within tolerance on clean and filled samples. |

## Acceptance criteria
- [ ] Kernel extensions in `Geometry.Linalg` (reuse if present): observation mask for gaps
      (`RobustPCAOptions` has no missing-entry set); thin SVD if F or 3M grows large.
- [ ] Paper intake recorded (low-rank mocap gap filling, PCP); marker-swap behaviour (two columns
      exchanging) analysed — entrywise sparsity may not capture it.
- [ ] CPU reference on synthetic data; all-missing markers and F < rank fail closed.
- [ ] Benchmark against linear/spline gap filling on the same data, sealed.
- [ ] New test suites `TrajectoryCleanupOperations`, `TrajectoryCleanupConfig` are added to `IntrinsicRuntimeContractTests`; they do not exist yet, and `--no-tests=error` cannot detect their absence while other selectors match.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(LinearAlgebra|TrajectoryCleanupOperations|TrajectoryCleanupConfig|AgentOperations|SandboxConfigSections|SandboxProcessingPanels|RuntimeEngineLayering|RuntimeEnginePrivateGlue)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```
