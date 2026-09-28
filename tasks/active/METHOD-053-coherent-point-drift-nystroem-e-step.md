---
id: METHOD-053
theme: I
depends_on: [METHOD-049]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note; implementation owes its own parity tests and sealed benchmark evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-053 — Nystroem E-step for Coherent Point Drift (opt-in, a-posteriori error)

## Goal
- Speed up the wide-kernel iterations of Coherent Point Drift, which dominate at 10^5
  points (C113: auto 198 s, nearly all in dense early iterations), with a Nystroem
  low-rank approximation of the E-step kernel matrix, as Hirose's BCPD implementation does
  for large sigma.

## Context
- METHOD-049 did not offer a Nystroem E-step because its error has no a-priori bound. This
  task adds it as an explicit opt-in policy whose error is estimated a posteriori (exact
  sampled rows) and reported; it is never selected by `Auto` unless the operator opts in,
  and results report the backend as approximate.
- Landmarks: the METHOD-049 farthest-point landmarks (or the selectable sampler once
  GEOM sampling selection lands).

## Acceptance criteria
- [x] `EStepPolicy::Nystrom` computing P1, Pt1, PX from L landmark columns (O((2M + N) L) per iteration), thread-count independent, with the sampled relative error reported as `EStepSampledError` (never presented as a bound).
- [x] Hand-over (Nystroem while sigma is wide, exact Auto choice of truncated/dense once narrow), built into the `Nystrom` policy rather than a flag on `Auto`, so `Auto` stays exact; landmarks double on rejection within half the dense cost.
- [ ] Parity against the reference on the METHOD-049 fixtures (registered-point delta and RMS to truth), and a sealed rerun of `geometry.coherent_point_drift.accelerated` with the new policy.
- [x] Config enum value, panel entry and agent field (the config enum mirrors `EStepPolicy`).

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged (two point spans). |
| Compatible entity sources | Every canonical domain via RUNTIME-273. |
| RuntimeModule | None; RUNTIME-273 passes the policy. |
| Config/agent | `sandbox.coherent_point_drift.e_step` value. |
| UI | CPD panel Performance node. |
| Publication | Unchanged. |
| End-to-end tests | Contract test through the editor command. |

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```
