---
id: METHOD-062
theme: I
depends_on: [GEOM-113, GRAPHICS-148, GRAPHICS-149, RUNTIME-290]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note planned with Fable 5.1 and Codex (2026-09-28); implementation owes its own tests, gpu;vulkan smokes and sealed evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-062 — Vulkan lazy greedy sampling over the point LBVH

## Goal
- Port `lazy_greedy.cu` on the engine's `Graphics.PointLbvhWorkspace` instead of the CUDA
  Karras build (document the node layout for the traversal shader); revealed-node hash
  tables become sorted keys (GRAPHICS-148) or int64 atomics when probed (GRAPHICS-149);
  bottom-up visit counters stay 32-bit.

## Acceptance criteria
- [ ] The declared beta guarantee checked on every prefix; parity with GEOM-113 as equal admissible sets (order may differ, documented); build-plus-sampling timing in a sealed benchmark.

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
