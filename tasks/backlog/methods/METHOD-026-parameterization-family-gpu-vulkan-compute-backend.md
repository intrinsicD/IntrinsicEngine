---
id: METHOD-026
theme: I
depends_on: [METHOD-025, RUNTIME-176]
maturity_target: ParityProven
---
# METHOD-026 — Parameterization family GPU (Vulkan compute) backend and parity

## Goal
- Add the `gpu_vulkan_compute` backend for the iterative parameterization strategies (ARAP, SLIM), executing the per-triangle local step and the global linear solve on the GPU, with an opt-in `gpu;vulkan` parity smoke against the CPU reference and a GPU-vs-CPU comparison benchmark — completing the backend matrix for the iterative strategies, so a caller can request GPU execution through the same surface and fall back honestly when no device is operational. The linear one-shot strategies (LSCM/SCP/BFF) stay CPU-only by recorded decision (see Non-goals).

## Non-goals
- No new strategy or numeric change — the GPU path must match the `METHOD-021`/`METHOD-022` reference within a documented parity tolerance and preserve SLIM injectivity.
- No GPU acceleration of the linear one-shot strategies (LSCM/SCP/BFF) — not in this task and not deferred from it. Their method tasks record no GPU follow-up (a one-shot sparse direct solve/eigensolve gains little from `gpu_vulkan_compute`); if a benchmark ever justifies one, it opens as its own method/backend task.
- No new GPU primitive library — reuse the shared `Extrinsic.Graphics.ComputeParallelPrimitives` (GRAPHICS-108) and the runtime GPU-queue/readback substrate rather than private CUB-equivalents.

## Context
- Owner/layer: a declared method backend adapter in `src/runtime`, the layer
  allowed to import RHI. This task introduces the runtime GPU request and
  requested/actual/fallback telemetry for ARAP/SLIM; the geometry strategy
  variant stays RHI-free and carries no family-wide GPU token.
- GPU shape: the local step (per-triangle signed-SVD rotation fit) is embarrassingly parallel; the global step is a sparse SPD solve run as a GPU Jacobi/CG iteration (matching the reference within tolerance). Results drain through `Extrinsic.Runtime.AsyncBufferReadback` (RUNTIME-137), never a device-wide `ReadBuffer` stall.
- Gating: reference parity (`METHOD-021`/`022`) and the optimized CPU baseline (`METHOD-025`) must exist first; the GPU backend is measured against the `METHOD-025` baseline and the CPU reference oracle. Renderer/runtime code gates on `RHI::IDevice::IsOperational()`; a GPU request on a non-operational device falls back to `cpu_reference`/`cpu_optimized` with honest telemetry.
- Config/UI: this task extends the config/result model delivered by retired
  `RUNTIME-176` and the panel delivered by retired `UI-036` with
  `gpu_vulkan_compute` after the implementation exists, and wires the new
  runtime GPU job queue. The satisfied dependency supplies the CPU
  facade/config path; it does not reserve an inert GPU request.

## Control surfaces
- Config/UI/Agent: add the runtime-owned `gpu_vulkan_compute` request for the
  supported iterative strategies and expose it through the existing validated
  config apply path; unavailable execution falls back honestly.

## Backends
- Backend axis: adds `gpu_vulkan_compute` with `gpu;vulkan` parity; CPU reference/optimized remain the oracles and the fallback.

## Required changes
- [ ] Add the runtime GPU backend adapter (mirroring
      `Runtime.KMeansGpuBackend` plus the Sandbox-editor-private K-Means queue
      attached to `Extrinsic.Runtime.SandboxEditorFacades`, whose queue DTOs
      remain on that public facade) that records the local-step and global-solve
      compute passes for ARAP/SLIM, uploads mesh topology/positions once,
      iterates on the GPU, and drains UVs through `AsyncBufferReadback`.
- [ ] Add and wire a runtime parameterization GPU job queue (JobService
      `GpuQueue` participant) so work records inside the renderer frame context
      with no extra present.
- [ ] Add the runtime/config backend request and requested/actual/fallback
      result telemetry; gate on `IDevice::IsOperational()` and fall back to CPU
      when unavailable.
- [ ] Preserve determinism within the documented GPU parity tolerance; preserve SLIM injectivity on the GPU path.

## Tests
- [ ] Opt-in `tests/integration/runtime/Test.ParameterizationGpuBackendGpuSmoke.cpp` labeled `gpu;vulkan` (mirroring `Test.KMeansGpuBackendGpuSmoke.cpp` — the adapter under test is runtime-owned): on a Vulkan-capable host the GPU ARAP/SLIM result matches the CPU reference within the documented parity tolerance, with zero flips for SLIM.
- [ ] Fallback: on the Null/non-operational device the runtime GPU request
      reports an actual CPU backend and `FellBackToCPU == true` (default gate).
- [ ] Determinism within tolerance across two GPU runs.

## Docs
- [ ] GPU-vs-CPU comparison benchmark manifest `benchmarks/geometry/manifests/parameterization_gpu_vs_cpu_smoke.yaml` (`benchmark_id: geometry.parameterization.gpu_vs_cpu.smoke`) recording GPU timing, CPU-reference/optimized baseline timing, and parity diagnostics; no speedup claim without this baseline.
- [ ] Update the ARAP/SLIM method READMEs' backend-status tables (`gpu_vulkan_compute` → `METHOD-026`) and the parameterization roadmap; note GPU numerical-tolerance limitations.

## Acceptance criteria
- [ ] `gpu_vulkan_compute` runs ARAP/SLIM on a Vulkan-capable host with CPU-reference parity within tolerance and preserved SLIM injectivity, cited from an actual `gpu;vulkan` run.
- [ ] GPU requests fall back honestly on non-operational devices with asserted telemetry (default CPU gate).
- [ ] The GPU-vs-CPU benchmark validates and runs; layering holds (geometry RHI-free; the adapter lives in runtime).

## Verification
```bash
# CPU gate (fallback + contract)
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Parameterization' -LE 'gpu|vulkan' --timeout 120
# Opt-in GPU parity (Vulkan-capable host)
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Parameterization' --timeout 180
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No numeric change versus the reference beyond documented parity tolerance; no speedup claim without the baseline benchmark.
- No live GPU work on the poll thread; readback drains through `AsyncBufferReadback`, not `IDevice::ReadBuffer`.
- No RHI import into `src/geometry`; no private GPU primitive library.

## Maturity
- Target: `Operational` on Vulkan-capable hosts and `ParityProven` against the CPU reference (mirroring `METHOD-020`). Requires the `ci-vulkan` preset run cited in `Verification`; CPU-only hosts stop at the asserted honest fallback. This closes the backend matrix for the iterative strategies; the linear strategies record no GPU follow-up in their own tasks.
