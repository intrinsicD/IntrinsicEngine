---
id: METHOD-056
theme: I
depends_on: [METHOD-053, GRAPHICS-149]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Finished interactively; the performance and parity result is ARA claim C117 with sealed, source-bound evidence in ara/evidence/diagnostics/method056_cpd_vulkan_e_step_20260929/.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-056 — Vulkan dense E-step for Coherent Point Drift

## Completion — 2026-09-29
Commit: `6a4f9b4d0` (seam, shaders, workspace, broker, config/panel/agent, tests),
`de87fd33f` (benchmark backend class, the sealed revision); the evidence commit on
`claude/cpd-nystrom` follows. ParityProven on the recorded host (C117); frame latency is
GRAPHICS-150.

Review round (2026-09-29, Fable 5.1 and Codex 6 Astra, medium effort) fixed: an fp32 precision
guard (first-order error estimate, iterations above 2e-5 stay on the CPU, the estimate is the
reported error bound instead of 0), the dispatch chunk capped at 65535 workgroups, device
iteration counts in results, and the pump job no longer releasing the workspace under a newer
step; a CPU contract test covers the broker's failure and timeout paths. The scaling run was
repeated and resealed after the guard (see C117).

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

- Numeric policy: `docs/architecture/compute-parallel-primitives.md` §"Device Capabilities And Exact-Arithmetic Policy" (GRAPHICS-149).

## Decisions (implementation, 2026-09-29)
- Seam as designed: `EStep::Settings::External` (`ExternalRequest`: target, moved, target
  generation, sigma^2, log c and log-weights in the frame shifted by the largest log-weight,
  output spans); `EStepPolicy::Vulkan` runs it where Auto would run dense, keeps truncation
  on the CPU, and otherwise falls back to that exact CPU choice (`Sums::ExternalFallback`,
  `Result::EStepFallbacks`). Backend reporting follows Nystroem: `gpu_vulkan_fp32_dense` when
  the device ran an iteration, else `cpu_auto`.
- Runtime owner is a per-run broker (`Runtime.CoherentPointDriftGpuEStep`), not a module
  participant: the step worker blocks in `Evaluate`; a companion pump job (High priority,
  trivial work) is polled through `IsReadyToApply` on the main thread every drain and records
  through `SpatialIndexCache::QueueGpuCompute`, which already owns framing and readback. A
  failure or a 60 s timeout closes the broker (later iterations do not wait); a run without
  the job lane never installs the evaluator (a synchronous wait would deadlock).
- Buffers are host-visible `CreateBuffer` allocations kept by the workspace across a run's
  iterations (target uploaded once per generation); the pump job releases them on the main
  thread when the step job ends, since a run can outlive the device.
- Kernels: fp32 distances and Cody-Waite exp (degree-7 polynomial), fp32 Kahan tile sums
  added to fp64 accumulators, fp64 log/exp for the per-row finalization; the source pass
  splits each fp64 log-denominator into fp32 high and low parts. Dispatches are bounded to
  2^30 kernel pairs. Devices without shader float64 are refused (CPU fallback with a
  diagnostic); the compensated-fp32 variant is not implemented.
- Latency: a device E-step takes 4-6 frames (submit, frames in flight, transfer readback);
  an off-frame compute submit stays a GRAPHICS follow-up.

## Acceptance criteria
- [x] Default gate: mock callback and Null host exercise the fallback; `Backend`/`RequestedBackend` truthful.
- [x] gpu;vulkan smoke `Test.CoherentPointDriftGpuEStepSmoke.cpp`: the tolerance above at three sigma values, weighted rows and outliers; two runs bitwise equal.
- [x] Manifest `coherent_point_drift_gpu_vulkan_smoke.yaml` (`intent: gpu`, actual backend required) and a sealed scaling run against CPU dense and Nystroem on the same fixture; ARA claim. (`coherent_point_drift_gpu_vulkan_scaling.yaml`, evidence `ara/evidence/diagnostics/method056_cpd_vulkan_e_step_20260929/`, C117: 9.8x over the CPU twin at 10^5 points.)
- [x] Config enum value, panel entry, agent field; one GPU CPD run at a time (JobService occupancy measured). (Each run has one step job at a time and its own broker; concurrent runs are not serialized globally: each blocks one worker while its device E-step is in flight, measured at 46% of a 10^5-point run's wall time. Frame latency is GRAPHICS-150.)

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
