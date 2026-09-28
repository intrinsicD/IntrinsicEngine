---
id: METHOD-055
theme: I
depends_on: [GEOM-111, GRAPHICS-108, GRAPHICS-149, RUNTIME-290]
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

## Design (2026-09-28 planning with Fable 5.1 and Codex)
- No persistent kernel: sieve state lives in buffers and `Extend` runs as chunks of at most
  256 selections per dispatch (GPU watchdog), with certified batches (mask <= 32) to cut
  barriers. Shaders `point_sampling_sieve_{build,select,update,refresh}.comp` and
  `point_sampling_common.glslinc`.
- Exactness: every double operation `precise` (the CUDA `__dsub_rn/__dmul_rn/__dadd_rn`), the
  one-ulp `nextafter` of the pruning bound via int64 bit operations, reductions
  subgroup-size agnostic or shared-memory trees, frontier/dirty arrays in a device buffer
  (portable shared memory is 16 KB), pair counters as plain per-workgroup stores (no 64-bit
  atomics). Hosts without fp64 fall back to the CPU sieve explicitly.
- One workgroup per cloud as in CUDA, so no speedup over the CPU sieve is expected for one
  cloud; the value is GPU-resident ordering and many clouds at once (`extend_many`).

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
