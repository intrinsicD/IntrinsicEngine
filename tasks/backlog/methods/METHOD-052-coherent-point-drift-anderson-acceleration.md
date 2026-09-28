---
id: METHOD-052
theme: I
depends_on: [METHOD-049]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note split out of METHOD-049; implementation owes its own parity tests and benchmark evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-052 — Anderson acceleration for Coherent Point Drift EM

## Goal
- Reduce the EM iteration count of Coherent Point Drift (all four variants) with Anderson
  mixing of the parameter updates, safeguarded so the objective never increases, as the
  option METHOD-015 and UI-055 deferred to METHOD-049 (not implemented there).

## Context
- Measured in METHOD-049 (C113): wide-kernel iterations dominate at 10^5 points (about
  9 s each, dense-bound), so fewer iterations is the remaining large lever.
- Framework24 offers Anderson on both transforms for BCPD; implement from the Anderson
  acceleration literature (e.g. Walker & Ni 2011; Zhang et al. 2019 for EM-type fixed
  points), not from that code.

## Acceptance criteria
- [ ] `Params::AndersonWindow` (0 = off) on `Geometry.Registration.CoherentPointDrift` with a monotonicity safeguard (fall back to the plain step when the objective rises).
- [ ] Same converged result as the plain EM within the METHOD-015/050 tolerances on their fixtures; fewer iterations reported on the scaling fixture.
- [ ] Config field, panel control and hint (UI-055 panel) and a smoke benchmark entry.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged (two point spans). |
| Compatible entity sources | Every canonical domain via RUNTIME-273. |
| RuntimeModule | None; RUNTIME-273 passes the option. |
| Config/agent | `sandbox.coherent_point_drift` field. |
| UI | CPD panel control. |
| Publication | Unchanged. |
| End-to-end tests | Contract test through the editor command. |

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
