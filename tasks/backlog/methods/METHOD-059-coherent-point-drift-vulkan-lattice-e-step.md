---
id: METHOD-059
theme: I
depends_on: [METHOD-058, METHOD-056, GRAPHICS-148]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note planned with Fable 5.1 and Codex (2026-09-28); implementation owes its own tests, gpu;vulkan smokes and sealed evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-059 — Vulkan permutohedral-lattice E-step for Coherent Point Drift (gated)

## Goal
- Port METHOD-058's lattice E-step to Vulkan (`cpd_lattice_{keys,splat,blur,slice}.comp`):
  lattice keys sorted and reduced deterministically (GRAPHICS-148) instead of float-atomic
  splats, blur axes as separate dispatches. Opens only when METHOD-058's sealed run shows the
  lattice beating GPU dense.

## Acceptance criteria
- [ ] Match the CPU lattice within a frozen budget; approximation measured separately against dense; repeated runs identical; bounded allocation (key overflow and vertex growth fail closed).

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged. |
| Compatible entity sources | Every canonical point domain. |
| RuntimeModule | METHOD-056 participant. |
| Config/agent | `e_step` `lattice` with backend reporting. |
| UI | Unchanged. |
| Publication | Unchanged. |
| End-to-end tests | gpu;vulkan smoke. |

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'CoherentPointDrift' -L 'gpu|vulkan' --timeout 300
```
