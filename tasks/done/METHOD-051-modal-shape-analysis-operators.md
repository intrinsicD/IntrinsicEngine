---
id: METHOD-051
theme: I
depends_on: [GEOM-024, UI-056]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Operator-requested interactive paper implementation; evidence is the diff, finite-difference and oracle tests, and the CPU gate.
contract_schema: 1
contracts: [repo.source-documentation, method.engine-integration, geometry.element-domain-sources, geometry.property-coherence]
---
# METHOD-051 — Modal shape analysis operators

## Completion — 2026-09-27
Commit: the enclosing `claude/modal-shape-operators` commit records this
retirement. CPUContracted: geometry kernels, runtime operation, config, panel and
tests below. The operation stays synchronous like UI-056.

## Goal
Implement the operators of Hildebrandt, Schulz, von Tycowicz and Polthier, "Modal
shape analysis beyond Laplacian" (CAGD 29(5), 2012; operator request 2026-09-27)
where they belong: kernels in geometry, eigenpairs through the GEOM-024 solver and
user access through the UI-056 spectral-modes operation.

## Formulation
- Modified Dirichlet energy `E_D^N(u) = ½ uᵀAu`, `A_ij = <N_i, N_j> S_ij` (eq. 12),
  exact cotan `S`, area-weighted unit normals.
- Discrete shells (Grinspun et al. 2003) in the general form of eq. 13 with
  flexural (`3|ē|²/Ā_e`, dihedral angle), length (`1/|ē|`) and area (`1/Ā`) terms;
  rest-state Hessian `Σ ω ∇f ∇fᵀ` (Lemma 1), 3#V rows, lumped mass per coordinate.
  Dihedral gradients follow Bridson et al. 2003 / Wardetzky et al. 2007.
- Signatures (eqs. 27, 30) and distance (eq. 31) from any eigenpairs, Sun et al.'s
  default scale range; with the cotan Laplacian this is the heat kernel signature.

## Acceptance criteria
- [x] `Geometry.ModalAnalysis`: `BuildModifiedDirichletMatrix`, `BuildThinShellHessian`, elementary gradients, `ExpandMass`, `DefaultScaleRange`, `ComputeModalSignature`, `ComputeModalDistance` with explicit empty, non-finite, non-triangular and invalid-parameter statuses.
- [x] Tests: `A = S` on planar meshes; `uᵀAu = Σ_k E_D(u N^k)` with independent normals; `E_D^N(1) → 4π` on refined unit spheres; dihedral and area gradients against central differences; `vᵀHv` against the second difference of an independent shell energy; translations and rotations in the Hessian nullspace and six zero vibration eigenvalues; `Σ m_v S_t(v) = Σ e^{-λt}`, zero distance at the source, symmetry and the triangle inequality.
- [x] `sandbox.laplacian_eigenbasis` gains operator, shell weights, skipped modes, signature and distance fields; defaults keep previous payloads valid. Mesh operators publish float (`E_D^N`) or vec3 (shell) modes over non-isolated vertices in the existing guarded transaction.
- [x] Panel renamed Spectral Modes with operator selection, shell weights, signature/distance section and show buttons; ImGui test publishes shell modes and a distance.
- [x] Contract tests match the operation to the geometry kernels solved directly, check the signature normalization and undo, bad distance sources and config validation.
- [x] Method doc, geometry architecture and method index updated.

## Non-goals
Animated vibration previews, viewport picking of the distance source, wave kernel
signatures, asynchronous execution and GPU backends.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Triangle-mesh vertex positions for the new operators; any canonical sample positions for the graph Laplacian. |
| Compatible entity sources | Mesh entities for `E_D^N` and the shell; the graph Laplacian keeps all eight domains (`geometry.element-domain-sources`). |
| RuntimeModule | Existing `Runtime.MeshFieldOperations.Eigenbasis.cpp` operation. |
| Config/agent | `sandbox.laplacian_eigenbasis`, one validator shared by file, agent and UI. |
| UI | View → Spectral Modes and the Mesh/Graph/PointCloud → Processing redirects. |
| Publication | Mode, signature and distance properties in one undoable transaction with stale-input guards. |
| End-to-end tests | `Test.ModalAnalysis.cpp`, `Test.LaplacianEigenbasisOperations.cpp`, `Test.SandboxProcessingPanels.cpp`. |

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --check
```
