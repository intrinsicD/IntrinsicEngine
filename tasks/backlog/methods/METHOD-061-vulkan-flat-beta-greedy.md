---
id: METHOD-061
theme: I
depends_on: [GEOM-113, RUNTIME-290]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note planned with Fable 5.1 and Codex (2026-09-28); implementation owes its own tests, gpu;vulkan smokes and sealed evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-061 — Vulkan flat beta-greedy / implicit MIS sampling

## Goal
- Port `flat_greedy.cu`: distance updates per batch, admission by U / beta, MIS rounds as
  bounded indirect dispatches (existing count-to-dispatch-args) with a device done flag,
  deterministic seeded priorities (Random, Clearance, CoverageGain) and the spherical-voxel
  index (sorted once at build, or GRAPHICS-148).

## Acceptance criteria
- [ ] beta = 1 equals exact FPS (METHOD-055); other beta equal GEOM-113's CPU sets per batch, then order; bounded rounds and overflow handling tested.

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
