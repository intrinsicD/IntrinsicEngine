---
id: METHOD-075
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: proposal filed from REVIEW-007 E7; blocked on missing measured-BRDF input and consumer; implementation follows the method workflow and owes research evidence before any claim.
contract_schema: 1
contracts: [method.engine-integration, repo.source-documentation]
contract_review: Input and output are measurement tables, not ECS geometry element domains or geometry properties, so geometry.element-domain-sources and geometry.property-coherence do not apply.
---
# METHOD-075 — RobustPCA cleanup of measured BRDF and measurement tables

## Goal
- Measured reflectance tables (e.g. MERL-style isotropic BRDFs in half/difference-angle
  parameterization): either one table reshaped to a matrix, or several materials as columns. The
  smooth reflectance structure / material space is **low-rank**; measurement artifacts, outliers and
  saturated samples are **sparse**; unmeasured angles are **missing**. Output a cleaned table and an
  artifact mask.
- Origin: REVIEW-007 E7 (2026-10-06), GE15 — application of `Geometry::Linalg::RobustPCA`
  (Principal Component Pursuit, L + S by ADMM; stopping rule fixed in `74135dfff`). This is a
  **proposal**; any fidelity statement needs an `ara/logic/claims.md` row first (AGENTS.md §8b).

## Missing infrastructure (prerequisite, not in scope)
- **Measured BRDF data:** no BRDF table import exists.
- **Consumer:** the renderer has no tabulated-BRDF material; a cleaned table is useful only once a
  consumer exists (tabulated material, or fitting to existing material parameters). Both must be
  filed and retired first. GEOIO-005 (CSV/NPY property tables) is the nearest generic table seam, but
  it binds tables to an element domain and does not cover a free matrix.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Dense numeric matrix with an observation mask (angles x materials, or reshaped angles). |
| Compatible entity sources | The BRDF/table asset from the prerequisite; geometry element domains N/A. |
| RuntimeModule | New table operation on existing editor processing commands, after the prerequisites. |
| Config/agent | `sandbox.table_low_rank`: table, reshaping, log/linear transform, lambda, tolerance; `preview_operation`/`run_operation`. |
| UI | Panel with rank, sparse fraction and a slice viewer, after the prerequisite consumer exists. |
| Publication | Cleaned table asset and mask through the prerequisite asset path; renderer binding is owned by the consumer task. |
| End-to-end tests | Synthetic analytic BRDF tables (e.g. sampled microfacet) with injected outliers → agent/panel → table within tolerance. |

## Acceptance criteria
- [ ] Prerequisites filed and retired: measured-BRDF/table import; a consumer of tabulated reflectance.
- [ ] Kernel extensions in `Geometry.Linalg` (reuse if present): observation mask; thin/partial SVD
      (full-U Jacobi SVD on a tall table allocates rows x rows).
- [ ] Paper intake recorded; literature to verify: Matusik et al. 2003 (data-driven reflectance),
      Lawrence et al. 2004 (factored BRDFs); value transform (log) and its effect on sparsity.
- [ ] CPU reference on synthetic tables; non-finite and negative measurements handled by policy.
- [ ] Benchmark against plain truncated SVD on the same tables, sealed.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure --timeout 60 --no-tests=error -R '^(LinearAlgebra|TableLowRankOperations|TableLowRankConfig|AgentOperations|RuntimeEngineLayering)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```
