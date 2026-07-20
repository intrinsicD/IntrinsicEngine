---
id: METHOD-020
theme: I
depends_on: [METHOD-019]
maturity_target: ParityProven
---
# METHOD-020 — LOP-family GPU (Vulkan compute) backend and parity

## Goal
- Evaluate an explicit command-recording `gpu_vulkan_compute` path for every
  LOP-family consolidation strategy (WLOP/LOP/CLOP/EAR), exposing it only for
  strategies that match the CPU reference within frozen tolerance. Prove each
  adopted path with an opt-in `gpu;vulkan` parity smoke and a GPU benchmark
  that reports its CPU-reference baseline.

## Non-goals
- No algorithm/variant changes; the GPU path reproduces the reference numerics, it does not redefine them.
- No second config lane or editor panel. Extend the delivered
  `RUNTIME-175`/`UI-035` surfaces only after the GPU path passes parity.
- No synchronous device-wide readback or work on the platform poll thread;
  this task owns the runtime adapter, JobService scheduling, shaders, and
  parity evidence.
- No speedup claim without the benchmark baseline; no new public parameters.

## Context
- Owner/layer: `src/runtime` (RHI-allowed), following the `docs/architecture/algorithm-variant-dispatch.md` split — the CPU reference stays in `src/geometry` with no RHI, and the GPU-capable path lives where `Extrinsic::RHI::IDevice&` is allowed, gated on `IDevice::IsOperational()` with honest `ActualBackend`/`FellBackToCPU` telemetry.
- Precedent to mirror: the K-Means GPU path (`GEOM-056` parity smoke + `Runtime.KMeansGpuBackend` explicit command recording) and progressive-Poisson `METHOD-014` (GPU operational parity). Reuse `Extrinsic.Graphics.ComputeParallelPrimitives` (`GRAPHICS-108`) for scan/compaction rather than private primitives, and drain readbacks through `Extrinsic.Runtime.AsyncBufferReadback` (`RUNTIME-137`), not `IDevice::ReadBuffer`.
- The projection iteration (neighbor-weighted attraction + repulsion, or the CLOP continuous per-component term) maps to a compute dispatch over the projected set; the shared weight math from `Geometry.PointCloud.Kernels` is reproduced in the shader with the same closed forms the CPU path uses.
- Verification requires the `ci-vulkan` preset and a Vulkan-capable host; on non-operational devices the path must fall back to the CPU reference with honest telemetry, and the parity test asserts that fallback.
- `METHOD-019` is an evidence-ordering gate, not a promise that every
  `cpu_optimized` strategy survived. Compare every GPU strategy against
  `cpu_reference`; include an optimized CPU comparison only where METHOD-019
  actually exposed that strategy.

## Control surfaces
- Config/UI/Agent: extend the runtime/app backend request with
  `gpu_vulkan_compute` only when this implementation exists, through the
  validated `RUNTIME-175` config/facade and `UI-035` presentation path. The
  geometry-owned CPU selector remains RHI-free.

## Backends
- Backend axis: adds `gpu_vulkan_compute`; `cpu_reference` stays the parity oracle and the fallback target.

## Slice plan
- **Slice A — runtime adapter/fallback.** Define the explicit record/submit/
  readback seam and prove Null-device fallback in the default CPU gate.
- **Slice B — one-strategy Vulkan parity.** Land WLOP/LOP first with actual
  operational-device evidence and frozen tolerance.
- **Slice C — CLOP/EAR parity.** Add each kernel path independently; reuse only
  already-landed shared GPU primitives.
- **Slice D — actual-GPU benchmark.** Emit and validate the dedicated result
  with CPU-reference baseline/device identity before any speed claim.

## Right-sizing
- One runtime adapter is justified by the CPU/GPU layer seam. Keep strategy
  dispatch inside it; do not create a backend registry or per-strategy service.
- Add one participant to the existing JobService `GpuQueue`; do not create a
  second queue, service, or synchronous device-wide readback path.

## Required changes
- [ ] Add `Runtime.ConsolidationGpuBackend` (module + implementation) exposing an explicit command-recording GPU projection for the consolidation strategies, taking `RHI::IDevice&`, gating on `IsOperational()`, and reusing persistent buffers across iterations.
- [ ] Add the consolidation participant to the existing JobService `GpuQueue`;
      submit through the real frame context and publish completed results
      through the delivered `RUNTIME-175` ECS writeback path.
- [ ] Add the compute shader assets for the attraction/repulsion (and CLOP continuous-term) passes under `assets/shaders/`, recorded through the RHI compute path.
- [ ] Reproduce the `Geometry.PointCloud.Kernels` weight/repulsion closed forms in-shader; document any float-precision divergence and bound it in the parity tolerance.
- [ ] Keep every projection iteration and convergence reduction on-device;
      drain the final result once through `AsyncBufferReadback`. No
      per-iteration CPU readback may steer convergence.
- [ ] Honest fallback + telemetry: a `Backend::GPU` request on a null/non-operational device runs the CPU reference and reports `FellBackToCPU`.
- [ ] Extend the delivered `RUNTIME-175` config/facade and `UI-035` panel with
      `gpu_vulkan_compute` only after the implementation exists; all UI,
      config-file, and agent requests use the same preview/validate/apply path.
- [ ] Update a package `method.yaml` to list `gpu_vulkan_compute` only if at
      least one strategy it contains passes parity; add the GPU benchmark
      manifest under `benchmarks` for the evaluated family. Record the exact
      per-strategy capability matrix in package docs and result diagnostics.

## Tests
- [ ] `tests/integration/runtime/Test.PointCloudConsolidationGpuParity.cpp`
      labeled `gpu;vulkan` (opt-in), asserting the runtime-owned adapter's GPU
      output matches the CPU reference within frozen per-strategy tolerances.
- [ ] Fallback telemetry: `Backend::GPU` requested on a non-operational/null device reports `ActualBackend == cpu_reference` and `FellBackToCPU == true`, verified in the default CPU gate (no GPU required).
- [ ] Config/control-surface parity: Editor, AgentCli, and Programmatic requests
      produce the same validated backend request; the panel never schedules
      GPU work from the poll thread. Unsupported strategy/backend pairs fail
      preview rather than silently running a different strategy.
- [ ] Determinism within the documented tolerance across two GPU runs on the same host.

## Docs
- [ ] GPU benchmark manifest
      `benchmarks/geometry/manifests/lop_family_gpu_vulkan_smoke.yaml` with
      stable ID `geometry.lop_family.gpu_vulkan.smoke`, a stable built-in
      dataset, `params.intent: gpu`, explicit warmup/measured counts,
      `baseline_comparison: cpu_reference_same_fixture`, and metrics
      `runtime_ms`/`gpu_time_ms`/`quality_error_l2`; exclude it from the
      default CPU smoke runner.
- [ ] Add `IntrinsicLopFamilyGpuBenchmarkSmoke`, emitting schema-valid result
      JSON only from actual Vulkan execution with backend/device, strategy,
      CPU-reference parity, fallback, timing source, and iteration diagnostics.
- [ ] Update each package README backend-status table (`gpu_vulkan_compute` → `METHOD-020`), the parity tolerance, and the shader/precision limitations.
- [ ] Note the GPU backend and its host requirement in the `docs/architecture/algorithm-variant-dispatch.md` current-exemplar section if the family becomes a cited exemplar.

## Acceptance criteria
- [ ] Every strategy is evaluated. Each exposed GPU strategy passes the
      `gpu;vulkan` parity smoke on a Vulkan-capable host and is cited in
      `Verification` as actually run; a miss stays CPU-only with recorded
      negative evidence.
- [ ] Fallback telemetry is asserted in the default CPU gate.
- [ ] The GPU benchmark validates and reports a CPU-reference baseline (no bare speedup claim).
- [ ] Where METHOD-019 exposed `cpu_optimized`, report it as an additional
      same-fixture comparison; do not fabricate an optimized baseline for a
      strategy that failed its adoption gate.
- [ ] The actual-GPU result validates; skipped/fallback execution is reported
      honestly and cannot satisfy the Vulkan acceptance row.
- [ ] No `Vk*` types cross the public seam.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Consolidation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests IntrinsicLopFamilyGpuBenchmarkSmoke
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Consolidation|IntrinsicLopFamilyGpuBenchmarkSmoke' --timeout 180
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/IntrinsicLopFamilyGpuBenchmarkSmoke --strict
python3 tools/agents/validate_method_manifests.py
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- No divergence from the reference numerics beyond the documented parity tolerance.
- No private scan/compaction primitives (reuse `ComputeParallelPrimitives`); no `IDevice::ReadBuffer` for the drain (use `AsyncBufferReadback`).
- No speedup claim without the baseline; no `Vk*` types on the public seam.

## Maturity
- Target: `Operational` on Vulkan-capable hosts and `ParityProven` against the
  CPU reference for every adopted strategy. Requires the `ci-vulkan` preset
  run cited in `Verification`; CPU-only hosts stop at the asserted honest
  fallback, and a parity miss remains CPU-only.
