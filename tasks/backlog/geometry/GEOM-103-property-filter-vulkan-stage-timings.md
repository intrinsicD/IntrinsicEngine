---
id: GEOM-103
theme: I
depends_on: [GEOM-081]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note split from GEOM-081; any resulting performance claim owes its own benchmark and ARA evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# GEOM-103 — Stage timings for the Vulkan property filters

## Goal

Measure what GEOM-081 left unmeasured: cold/warm transfer, compute and readback
time and device memory of the Vulkan explicit property filters, against the CPU
reference on representative sizes.

## Context

GEOM-081 proved parity (bitwise for the linear filters, `9.2e-17` relative for
bilateral on an RTX 3050) and seals an end-to-end, frame-paced editor runtime
under `build/ci-vulkan/benchmark-ctest/GEOM-081`. That number includes present
pacing (about 1 Hz on a locked desktop seat, faster under Xephyr) and says
nothing about kernel cost. The RHI profiler plans timestamp scopes per frame, so
per-job scopes need either profiler integration in `Graphics.PropertyFilter`
recording or a standalone producer that drives the workspace directly.

## Acceptance criteria

- [ ] Timestamp scopes (or an equivalent standalone producer) separate upload, dispatch and readback for cold and warm runs on at least two graph sizes, plus the buffer bytes allocated.
- [ ] A manifest-backed benchmark under a `GEOM103Vulkan` selector records them next to the CPU reference; no speedup is claimed without ARA evidence.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged from GEOM-081. |
| Compatible entity sources | Unchanged from GEOM-081. |
| RuntimeModule | Measurement only; no new runtime surface. |
| Config/agent | None. |
| UI | None. |
| Publication | None; benchmark results only. |
| End-to-end tests | The benchmark producer and its validation test. |

## Verification

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'GEOM103Vulkan' --no-tests=error --timeout 180
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/GEOM-103 --strict
```
