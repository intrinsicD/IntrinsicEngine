---
id: UI-056
theme: I
depends_on: [GEOM-024]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive follow-up of GEOM-024; evidence is the diff, tests and CI.
contract_schema: 1
contracts: [method.engine-integration, geometry.element-domain-sources, geometry.property-coherence]
---
# UI-056 — Laplacian eigenbasis viewer

## Completion — 2026-09-27
Commit: the enclosing `claude/geom-024-sparse-eigensolver` commit records this
retirement. CPUContracted: operation, config, panel with spectrum chart, contract
tests on graph, point-cloud and mesh domains, and a real ImGui action test. The
operation is synchronous; an asynchronous job is a follow-up only if large meshes
need it.

## Goal
Make the GEOM-024 eigensolver user-visible, matching Framework24's
eigendecomposition viewer: compute the k smallest eigenpairs of a sample-graph
Laplacian, publish the eigenvectors as scalar properties and show the eigenvalues
as a bar chart with a per-eigenvector show button.

## Acceptance criteria
- [x] Operation on every canonical domain through the shared sample graphs (kNN, uniform mesh edges, nonnegative cotangent) with unit or lumped-area mass (mesh vertices); `A = D - W`, `M` diagonal.
- [x] Publishes `k` float properties `<prefix><j>` in one guarded undoable transaction; existing outputs of another type are rejected; stale inputs reject undo/redo.
- [x] Result reports eigenvalues, backward-error residuals, iterations and backend; non-convergence publishes nothing.
- [x] Config section `sandbox.laplacian_eigenbasis` with one validator; panel View → Laplacian Eigenbasis plus domain redirects, eigenvalue bar chart and show buttons.
- [x] Contract tests (analytic path-graph spectrum on a graph entity, cotangent mesh with lumped mass, config validation, undo/redo) and a real ImGui action test.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Sample positions of any canonical domain plus graph weights, mass choice and k. |
| Compatible entity sources | All eight canonical domains via the shared property-graph capture. |
| RuntimeModule | `Runtime.MeshFieldOperations.Eigenbasis.cpp`, reusing the property-graph helpers. |
| Config/agent | `sandbox.laplacian_eigenbasis`. |
| UI | View → Laplacian Eigenbasis and Mesh/Graph/PointCloud → Processing redirects. |
| Publication | `k` float properties in one transaction. |
| End-to-end tests | Contract tests and the ImGui action test. |

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Eigenbasis|SparseEigensolver|SandboxProcessingPanels' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
