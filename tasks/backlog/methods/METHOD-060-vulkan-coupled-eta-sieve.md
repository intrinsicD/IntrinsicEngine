---
id: METHOD-060
theme: I
depends_on: [GEOM-113, METHOD-055]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note planned with Fable 5.1 and Codex (2026-09-28); implementation owes its own tests, gpu;vulkan smokes and sealed evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-060 — Vulkan coupled eta-sieve

## Goal
- Port the operator's `gpu_coupled_sieve.cu`: one workgroup per Morton leaf for the bulk
  update, per-leaf priority reduce and a fixed-order global argmax; the batch loop as
  host-driven rounds or indirect dispatch with a device batch-closed flag (Vulkan has no
  grid-wide sync). The leaf partition is static (sorted on the CPU at upload); 64-bit
  counters become per-leaf 32-bit slots.

## Acceptance criteria
- [ ] eta = 1 reproduces METHOD-055's order bit for bit; other eta equal the GEOM-113 CPU coupled sieve; adversarial-bound and stale-maximum tests.
- [ ] Sealed comparison against the CPU sieve and METHOD-055 at 10^5 and 10^6 points.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Point positions (any point domain) plus sampling parameters. |
| Compatible entity sources | Point clouds, graph nodes, mesh vertices via `Geometry.PointSampling`. |
| RuntimeModule | RUNTIME-290 pipeline set. |
| Config/agent | Sampling method and backend fields (RUNTIME-289/274). |
| UI | Point Sampling panel backend combo. |
| Publication | Same as the CPU method. |
| End-to-end tests | gpu;vulkan parity smoke. |

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'PointSampling' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'PointSampling' -L 'gpu|vulkan' --timeout 300
```
