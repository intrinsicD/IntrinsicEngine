---
id: METHOD-058
theme: I
depends_on: [GEOM-060, METHOD-053]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note planned with Fable 5.1 and Codex (2026-09-28); implementation owes its own tests, gpu;vulkan smokes and sealed evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-058 — Permutohedral-lattice E-step for Coherent Point Drift (FilterReg), CPU

## Goal
- Cover the middle sigma phase (too narrow for Nystroem, too wide for truncation; dense
  today) with the Gaussian-filter E-step of FilterReg (Gao & Tedrake, CVPR 2019):
  S_n from splat(sources, weights) / slice(targets), P1 and PX from
  splat(targets, (1, x)/den) / slice(sources); `EStepPolicy::Lattice` (backend
  `cpu_lattice_approx`) with the same a-posteriori sampled error and exact fallback as
  Nystroem (no a-priori bound). Paper intake first; this is approximate CPD, not FilterReg's
  full objective.

## Acceptance criteria
- [ ] P1, Pt1, PX, matched mass, likelihood and final alignment against dense within frozen tolerances; the accepted sigma band documented (underflow without a log-sum-exp shift is detected and falls back).
- [ ] Sealed rerun of `geometry.coherent_point_drift.accelerated` with the lattice; ARA claim.
- [ ] Decides METHOD-059: the GPU lattice opens only if this run beats METHOD-056's GPU dense at >= 10^6 points or on non-Vulkan hosts.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged. |
| Compatible entity sources | Every canonical point domain. |
| RuntimeModule | None; RUNTIME-273 passes the policy. |
| Config/agent | `e_step` value `lattice`. |
| UI | CPD panel Performance node. |
| Publication | Unchanged. |
| End-to-end tests | Contract test through the editor command. |

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift|PermutohedralLattice' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
```
