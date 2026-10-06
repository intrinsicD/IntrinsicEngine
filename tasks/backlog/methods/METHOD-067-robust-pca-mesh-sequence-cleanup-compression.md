---
id: METHOD-067
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
# METHOD-067 — RobustPCA cleanup and compression of fixed-topology mesh sequences

## Goal
- Stack F frames of a deforming mesh with fixed connectivity as a 3V x F matrix (one column per
  frame, optionally after removing the per-frame rigid motion). **Low-rank L:** the coherent
  deformation subspace. **Sparse S:** per-vertex, per-frame spikes (scan noise, tracking or
  correspondence errors). Cleanup publishes L frames; compression reports rank and the size of the
  truncated factors plus S.
- Origin: REVIEW-007 E7 (2026-10-06), GE15 — application of `Geometry::Linalg::RobustPCA`
  (Principal Component Pursuit, L + S by ADMM; stopping rule fixed in `74135dfff`). This is a
  **proposal**; any compression ratio or error statement needs an `ara/logic/claims.md` row first
  (AGENTS.md §8b).

## Missing infrastructure (prerequisite, not in scope)
- The engine has no mesh-sequence/animated-mesh asset or playback. This task works on an ordered
  selection of separate mesh entities with identical connectivity (e.g. per-frame OBJ imports of the
  Vlasic sequences). Storing a compressed sequence as an asset, or playing it back, needs a
  sequence asset task filed first; it is not silently added here.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Ordered list of F `vec3` position sets of equal cardinality and index correspondence. |
| Compatible entity sources | Mesh vertices (fixed connectivity checked), and equally point-cloud points / graph nodes with index correspondence. |
| RuntimeModule | New `Runtime.SequenceLowRankOperations` on existing editor processing commands (multi-entity input). |
| Config/agent | `sandbox.sequence_low_rank`: frame order (selection or name), rigid pre-alignment on/off, lambda, tolerance, rank tolerance; `preview_operation`/`run_operation`. |
| UI | Panel over the current multi-selection with frame-order list and result diagnostics (rank, sparse fraction, residual). |
| Publication | Per frame: cleaned positions (one undo step for the whole sequence) and a per-vertex scalar sparse magnitude; compressed asset deferred to the sequence-asset prerequisite. |
| End-to-end tests | Synthetic low-rank sequence with injected spikes → panel/agent → cleaned frames within tolerance; mismatched connectivity rejected. |

## Acceptance criteria
- [ ] Kernel extension in `Geometry.Linalg` (first slice, CPU tests; reuse if another RobustPCA task
      already added it): thin/partial SVD — `ComputeSVD` runs `Eigen::JacobiSVD` with full U and V per
      ADMM iteration, so a 3V x F matrix allocates 3V x 3V.
- [ ] Paper intake recorded (vertex-trajectory PCA compression, PCP); scaling of 3V x F matrices
      decided (thin SVD, column normalization).
- [ ] CPU reference recovers a synthetic rank-r deformation with sparse spikes; identical-frame and
      single-frame inputs are handled; connectivity mismatch fails closed.
- [ ] Benchmark on a declared fixed-topology sequence (verify the dataset keeps vertex correspondence
      across frames) reports reconstruction error vs rank and sparse count, sealed.
- [ ] Panel and agent share config and apply; docs record limits (no topology change, memory bound).
- [ ] New test suites `SequenceLowRankOperations`, `SequenceLowRankConfig` are added to `IntrinsicRuntimeContractTests`; they do not exist yet, and `--no-tests=error` cannot detect their absence while other selectors match.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(LinearAlgebra|SequenceLowRankOperations|SequenceLowRankConfig|AgentOperations|SandboxConfigSections|SandboxProcessingPanels|RuntimeEngineLayering|RuntimeEnginePrivateGlue)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```

## Context
- METHOD-072 (trajectory cleanup) shares the stacked-sequence input; whichever lands first owns the
  multi-entity sequence capture and the other reuses it.
