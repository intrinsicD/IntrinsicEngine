---
id: METHOD-026
theme: I
depends_on: [METHOD-025]
maturity_target: Operational
---
# METHOD-026 — Parameterization family GPU (Vulkan compute) backend and parity

## Goal
- Add the `gpu_vulkan_compute` backend for the iterative parameterization strategies (ARAP, SLIM), executing the per-triangle local step and the global linear solve on the GPU, with an opt-in `gpu;vulkan` parity smoke against the CPU reference and a GPU-vs-CPU comparison benchmark — completing the family's Strategy × Backend matrix so a caller can request GPU execution through the same surface and fall back honestly when no device is operational.

## Non-goals
- No new strategy or numeric change — the GPU path must match the `METHOD-021`/`METHOD-022` reference within a documented parity tolerance and preserve SLIM injectivity.
- No GPU acceleration of the linear strategies (LSCM/SCP/BFF) in this task.
- No new GPU primitive library — reuse the shared `Extrinsic.Graphics.ComputeParallelPrimitives` (GRAPHICS-108) and the runtime GPU-queue/readback substrate rather than private CUB-equivalents.

## Context
- Owner/layer: a declared method backend adapter in `src/runtime` (the layer allowed to import RHI), behind the `Backend::GPU` request on the `Geometry.Parameterization` surface — geometry stays RHI-free. Mirrors the `Extrinsic.Runtime.KMeansGpuBackend` / `KMeansGpuJobQueue` exemplar and the GEOM-056 opt-in `gpu;vulkan` parity smoke pattern.
- GPU shape: the local step (per-triangle signed-SVD rotation fit) is embarrassingly parallel; the global step is a sparse SPD solve run as a GPU Jacobi/CG iteration (matching the reference within tolerance). Results drain through `Extrinsic.Runtime.AsyncBufferReadback` (RUNTIME-137), never a device-wide `ReadBuffer` stall.
- Gating: reference parity (`METHOD-021`/`022`) and the optimized CPU baseline (`METHOD-025`) must exist first; the GPU backend is measured against the `METHOD-025` baseline and the CPU reference oracle. Renderer/runtime code gates on `RHI::IDevice::IsOperational()`; a GPU request on a non-operational device falls back to `cpu_reference`/`cpu_optimized` with honest telemetry.
- Config/UI: the `Backend::GPU` request is already the surface the `RUNTIME-176` config lane and `UI-036` panel expose; this task makes that request execute on the GPU and wires the runtime GPU job-queue leg reserved by `RUNTIME-176`.

## Control surfaces
- Config/UI/Agent: none new — `gpu_vulkan_compute` is the existing `Backend::GPU`/policy token; this task makes it operational and honest under fallback.

## Backends
- Backend axis: adds `gpu_vulkan_compute` with `gpu;vulkan` parity; CPU reference/optimized remain the oracles and the fallback.

## Required changes
- [ ] Add the runtime GPU backend adapter (mirroring `Runtime.KMeansGpuBackend`/`KMeansGpuJobQueue`) that records the local-step and global-solve compute passes for ARAP/SLIM, uploads mesh topology/positions once, iterates on the GPU, and drains UVs through `AsyncBufferReadback`.
- [ ] Wire the adapter into the `RUNTIME-176` GPU job-queue leg (JobService `GpuQueue` participant) so the work records inside the renderer frame context with no extra present.
- [ ] Gate on `IDevice::IsOperational()`; fall back to CPU with `RequestedBackend == GPU`, `ActualBackend == cpu_*`, `FellBackToCPU == true` when unavailable.
- [ ] Preserve determinism within the documented GPU parity tolerance; preserve SLIM injectivity on the GPU path.

## Tests
- [ ] Opt-in `tests/gpu/geometry/Test.ParameterizationGpuParity.cpp` labeled `gpu;vulkan`: on a Vulkan-capable host the GPU ARAP/SLIM result matches the CPU reference within the documented parity tolerance, with zero flips for SLIM.
- [ ] Fallback: on the Null/non-operational device a `Backend::GPU` request returns `ActualBackend == cpu_*` with `FellBackToCPU == true` (runs in the default CPU gate).
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
- Target: `Operational` on Vulkan-capable hosts (`gpu;vulkan` parity smoke cited); `CPUContracted` fallback everywhere else. This closes the family's Strategy × Backend matrix.
