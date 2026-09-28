---
id: METHOD-055
theme: I
depends_on: [GEOM-111, GRAPHICS-108]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note; implementation owes GPU parity smokes and benchmark evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-055 — Vulkan compute port of the hole-sieve farthest-point sampler

## Goal
- Port the operator's CUDA GPU hole sieve (persistent traversal, certified batches,
  multi-node `extend_many`) to Vulkan compute, bit-exact against the GEOM-111 CPU order.

## Acceptance criteria
- [ ] Vulkan backend selectable in `PointSampling`, order equal to the CPU reference on the parity fixtures (gpu;vulkan smoke).
- [ ] Sealed benchmark against the CPU sieve at 10^5 and 10^6 points.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Point positions (any point domain) plus sampling parameters. |
| Compatible entity sources | Point clouds, graph nodes, mesh vertices through `Geometry.PointSampling`. |
| RuntimeModule | Backend selector in the sampling operation (RUNTIME-274) and consumers (RUNTIME-289). |
| Config/agent | Backend field of the shared sampling config. |
| UI | Backend combo in the sampling widget. |
| Publication | Unchanged (same order as the CPU reference). |
| End-to-end tests | gpu;vulkan parity smoke against the CPU order. |

## Verification
```bash
ctest --test-dir build/ci-vulkan --output-on-failure -R 'PointSampling' -L 'gpu|vulkan' --timeout 300
```
