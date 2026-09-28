---
id: METHOD-056
theme: I
depends_on: [METHOD-053, GRAPHICS-149]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note planned with Fable 5.1 and Codex (2026-09-28); implementation owes its own tests, gpu;vulkan smokes and sealed evidence.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-056 — Vulkan dense E-step for Coherent Point Drift

## Goal
- Evaluate the CPD E-step statistics (P1, Pt1, PX, LogDenominatorSum, Matched) on the GPU so
  a 10^5-point iteration takes a fraction of a second instead of 8.8 s (CPU dense, C113),
  with the M-step on the CPU. Fable 5.1 and Codex planned it independently and converge on
  this design.

## Design (decided)
- Seam, geometry stays core-only: `EStep::Settings` gains an optional external evaluator
  callback (moved SoA spans, sigma^2, log c, source log-weights, tolerance -> `Sums`);
  `EStepPolicy::Vulkan` (backend `gpu_vulkan_fp32_dense`) routes to it and falls back to the
  exact Auto choice when it is absent or fails, recording the reason. Narrow-kernel
  iterations (truncated radius below the extent) run on the CPU truncated path, the hand-over
  METHOD-053 already uses.
- Runtime owner `src/runtime/Modules/Registration/Runtime.CoherentPointDriftGpuEStep.{cppm,cpp}`
  following `Runtime.PointCloudConsolidationGpu.cpp`: JobService GPU participant, BDA state,
  `BufferManager` leases, target uploaded once per run, moved source per iteration,
  `GpuTransfer` batched readback; the solver worker waits for the frame-paced result
  (2-3 frames; an off-frame compute submit is a later GRAPHICS follow-up if latency matters).
- Kernels `assets/shaders/cpd_estep_target_pass.comp` (per target row: online log-sum-exp
  over shared-memory source tiles, fp32 distances and exp, fp64 accumulation; compensated
  fp32 when `SupportsShaderFloat64()` is false) and `cpd_estep_source_pass.comp` (per source:
  sum exp(a - logden_n) (1, x_n), fp64 accumulation): the CPU two-pass form, no dense P
  matrix, no float atomics; scalar sums on the CPU in fixed order. Weighted (Bayesian) rows
  and the outlier term as on the CPU.
- Precision claim (frozen before measuring): denominators and P1 relative <= 1e-5 against
  CPU dense on the METHOD-049 fixtures, registered points <= 1e-4 (the cap the manifest
  applies to approximate policies), plus registration-quality gates; deterministic on one
  device and driver, not bitwise across vendors. An fp64-pairs diagnostic mode exists for
  parity investigations only.

## Acceptance criteria
- [ ] Default gate: mock callback and Null host exercise the fallback; `Backend`/`RequestedBackend` truthful.
- [ ] gpu;vulkan smoke `Test.CoherentPointDriftGpuEStepSmoke.cpp`: the tolerance above at three sigma values, weighted rows and outliers; two runs bitwise equal.
- [ ] Manifest `coherent_point_drift_gpu_vulkan_smoke.yaml` (`intent: gpu`, actual backend required) and a sealed scaling run against CPU dense and Nystroem on the same fixture; ARA claim.
- [ ] Config enum value, panel entry, agent field; one GPU CPD run at a time (JobService occupancy measured).

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Two point spans (unchanged). |
| Compatible entity sources | Every canonical point domain via RUNTIME-273. |
| RuntimeModule | New `Runtime.CoherentPointDriftGpuEStep` participant; RUNTIME-273 injects it. |
| Config/agent | `e_step` value `vulkan` in `sandbox.coherent_point_drift`. |
| UI | CPD panel Performance node. |
| Publication | Unchanged. |
| End-to-end tests | Contract test with the mock seam; gpu;vulkan smoke through the editor command. |

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'CoherentPointDrift' -L 'gpu|vulkan' --timeout 300
```
