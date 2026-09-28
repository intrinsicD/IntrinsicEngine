---
id: METHOD-057
theme: I
depends_on: [METHOD-056]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note planned with Fable 5.1 and Codex (2026-09-28); implementation owes its own tests, gpu;vulkan smokes and sealed evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-057 — Vulkan truncated and Nystroem E-steps for Coherent Point Drift (gated)

## Goal
- Extend METHOD-056's GPU seam to the truncated E-step (radius search over
  `Graphics.PointLbvhWorkspace`) and the Nystroem cross-kernel products (the landmark solve
  stays on the CPU), reporting hybrid execution explicitly. The fast Gauss transform is not
  ported (slower than dense in 3-D, C113). Codex proposed this task; Fable judged GPU dense
  sufficient. It therefore opens only if METHOD-056's sealed run shows the CPU tail or the
  wide phase dominating GPU runtime at 10^6 points.

## Acceptance criteria
- [ ] Truncation bound and completeness preserved (GPU roundoff accounted separately from the bound); Nystroem sampled-error rejection preserved.
- [ ] Parity against the CPU policies within METHOD-056's frozen tolerance; unsupported-policy fallback tested.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged. |
| Compatible entity sources | Every canonical point domain. |
| RuntimeModule | METHOD-056 participant. |
| Config/agent | Existing `e_step` values with backend reporting. |
| UI | Unchanged. |
| Publication | Unchanged. |
| End-to-end tests | gpu;vulkan smoke per policy. |

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'CoherentPointDrift' -L 'gpu|vulkan' --timeout 300
```
