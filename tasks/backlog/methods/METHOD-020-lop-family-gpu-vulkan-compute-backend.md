---
id: METHOD-020
theme: I
depends_on: [METHOD-019]
maturity_target: ParityProven
---
# METHOD-020 — LOP-family GPU (Vulkan compute) backend and parity

## Goal
- Add the `gpu_vulkan_compute` backend for the LOP-family consolidation projection (WLOP/LOP/CLOP/EAR) as an explicit command-recording path that matches the CPU reference within a documented parity tolerance, proven by an opt-in `gpu;vulkan` parity smoke and a GPU benchmark that reports its CPU-reference baseline.

## Non-goals
- No algorithm/variant changes; the GPU path reproduces the reference numerics, it does not redefine them.
- No synchronous convenience overload, config-lane, editor panel, or JobService scheduling here — those are `RUNTIME-175`/`UI-035`. This task owns the GPU dispatch, shaders, and parity only.
- No speedup claim without the benchmark baseline; no new public parameters.

## Context
- Owner/layer: `src/runtime` (RHI-allowed), following the `docs/architecture/algorithm-variant-dispatch.md` split — the CPU reference stays in `src/geometry` with no RHI, and the GPU-capable path lives where `Extrinsic::RHI::IDevice&` is allowed, gated on `IDevice::IsOperational()` with honest `ActualBackend`/`FellBackToCPU` telemetry.
- Precedent to mirror: the K-Means GPU path (`GEOM-056` parity smoke + `Runtime.KMeansGpuBackend` explicit command recording) and progressive-Poisson `METHOD-014` (GPU operational parity). Reuse `Extrinsic.Graphics.ComputeParallelPrimitives` (`GRAPHICS-108`) for scan/compaction rather than private primitives, and drain readbacks through `Extrinsic.Runtime.AsyncBufferReadback` (`RUNTIME-137`), not `IDevice::ReadBuffer`.
- The projection iteration (neighbor-weighted attraction + repulsion, or the CLOP continuous per-component term) maps to a compute dispatch over the projected set; the shared weight math from `Geometry.PointCloud.Kernels` is reproduced in the shader with the same closed forms the CPU path uses.
- Verification requires the `ci-vulkan` preset and a Vulkan-capable host; on non-operational devices the path must fall back to the CPU reference with honest telemetry, and the parity test asserts that fallback.

## Control surfaces
- Config/UI/Agent: none new — the backend request is the existing `Backend::GPU` value on the consolidation params, surfaced by `RUNTIME-175`/`UI-035`.

## Backends
- Backend axis: adds `gpu_vulkan_compute`; `cpu_reference` stays the parity oracle and the fallback target.

## Required changes
- [ ] Add `Runtime.ConsolidationGpuBackend` (module + implementation) exposing an explicit command-recording GPU projection for the consolidation strategies, taking `RHI::IDevice&`, gating on `IsOperational()`, and reusing persistent buffers across iterations.
- [ ] Add the compute shader assets for the attraction/repulsion (and CLOP continuous-term) passes under `assets/shaders/`, recorded through the RHI compute path.
- [ ] Reproduce the `Geometry.PointCloud.Kernels` weight/repulsion closed forms in-shader; document any float-precision divergence and bound it in the parity tolerance.
- [ ] Honest fallback + telemetry: a `Backend::GPU` request on a null/non-operational device runs the CPU reference and reports `FellBackToCPU`.
- [ ] Update the three `method.yaml` files to list `gpu_vulkan_compute` in `backends` and to add the GPU benchmark manifest under `benchmarks`.

## Tests
- [ ] `tests/gpu/geometry/Test.PointCloudConsolidationGpuParity.cpp` labeled `gpu;vulkan` (opt-in), asserting GPU output matches the CPU reference within tolerance for the standard fixtures.
- [ ] Fallback telemetry: `Backend::GPU` requested on a non-operational/null device reports `ActualBackend == cpu_reference` and `FellBackToCPU == true`, verified in the default CPU gate (no GPU required).
- [ ] Determinism within the documented tolerance across two GPU runs on the same host.

## Docs
- [ ] GPU benchmark manifest `benchmarks/geometry/manifests/lop_family_gpu_vulkan_smoke.yaml` (`params.intent: gpu`, `baseline_comparison: cpu_reference_same_fixture`, metrics `runtime_ms`/`gpu_time_ms`/`quality_error_l2`), excluded from the default CPU smoke runner.
- [ ] Update each package README backend-status table (`gpu_vulkan_compute` → `METHOD-020`), the parity tolerance, and the shader/precision limitations.
- [ ] Note the GPU backend and its host requirement in the `docs/architecture/algorithm-variant-dispatch.md` current-exemplar section if the family becomes a cited exemplar.

## Acceptance criteria
- [ ] The `gpu;vulkan` parity smoke passes on a Vulkan-capable host and is cited in `Verification` as actually run.
- [ ] Fallback telemetry is asserted in the default CPU gate.
- [ ] The GPU benchmark validates and reports a CPU-reference baseline (no bare speedup claim).
- [ ] No `Vk*` types cross the public seam.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Consolidation' --timeout 180
# CPU-only fallback telemetry stays in the default gate:
ctest --test-dir build/ci --output-on-failure -R 'Consolidation' -LE 'gpu|vulkan' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/agents/validate_method_manifests.py --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No divergence from the reference numerics beyond the documented parity tolerance.
- No private scan/compaction primitives (reuse `ComputeParallelPrimitives`); no `IDevice::ReadBuffer` for the drain (use `AsyncBufferReadback`).
- No speedup claim without the baseline; no `Vk*` types on the public seam.

## Maturity
- Target: `Operational` on Vulkan-capable hosts and `ParityProven` against the CPU reference. Requires the `ci-vulkan` preset run cited in `Verification`; CPU-only hosts stop at the asserted honest fallback.
