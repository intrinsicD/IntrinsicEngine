---
id: METHOD-026
theme: I
depends_on: [METHOD-025, RUNTIME-176, UI-036]
maturity_target: ParityProven
---
# METHOD-026 — Parameterization family GPU (Vulkan compute) backend and parity

## Goal
- Evaluate `gpu_vulkan_compute` for both iterative parameterization strategies
  (ARAP and SLIM), executing the per-triangle local step and global solve on
  the GPU. Expose it per strategy only after an actual `gpu;vulkan` run proves
  CPU-reference parity (and SLIM injectivity), with a GPU-vs-CPU comparison
  result and honest CPU-reference fallback. Linear one-shot strategies remain
  CPU-only by recorded decision.

## Non-goals
- No new strategy or numeric change — the GPU path must match the `METHOD-021`/`METHOD-022` reference within a documented parity tolerance and preserve SLIM injectivity.
- No GPU acceleration of the linear one-shot strategies (LSCM/SCP/BFF) — not in this task and not deferred from it. Their method tasks record no GPU follow-up (a one-shot sparse direct solve/eigensolve gains little from `gpu_vulkan_compute`); if a benchmark ever justifies one, it opens as its own method/backend task.
- No new GPU primitive library — reuse the shared `Extrinsic.Graphics.ComputeParallelPrimitives` (GRAPHICS-108) and the runtime GPU-queue/readback substrate rather than private CUB-equivalents.

## Context
- Owner/layer: a declared method backend adapter in `src/runtime`, the layer
  allowed to import RHI. This task introduces the runtime GPU request and
  requested/actual/fallback telemetry for ARAP/SLIM; the geometry strategy
  variant stays RHI-free and carries no family-wide GPU token.
- GPU shape: the local step (per-triangle signed-SVD rotation fit) is
  embarrassingly parallel; the global step is a sparse SPD solve run as a GPU
  Jacobi/CG iteration (matching the reference within tolerance). SLIM energy,
  signed-area, and line-search reductions remain on-device throughout the
  bounded iteration. Results drain once through
  `Extrinsic.Runtime.AsyncBufferReadback` (RUNTIME-137), never through a
  per-iteration CPU round trip or device-wide `ReadBuffer` stall.
- Gating: reference parity (`METHOD-021`/`022`) and completion of the
  SLIM-only `METHOD-025` optimized-CPU evaluation must exist first. The CPU
  reference is always canonical; compare against `cpu_optimized` only for SLIM
  if METHOD-025's adoption gate passed. ARAP has no optimized CPU baseline.
  Renderer/runtime code gates on
  `RHI::IDevice::IsOperational()`; a GPU request on a non-operational device
  falls back to `cpu_reference` with honest telemetry.
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
- Backend axis: adds `gpu_vulkan_compute` with `gpu;vulkan` parity.
  `cpu_reference` is always the oracle and fallback; an adopted SLIM
  `cpu_optimized` path is an additional comparison, never the ARAP oracle.

## Slice plan
- **Slice A — runtime adapter/fallback.** Define the record/submit/readback
  contract and prove honest Null-device fallback in the default gate.
- **Slice B — ARAP Vulkan parity.** Land and verify one iterative strategy on
  an operational device.
- **Slice C — SLIM Vulkan parity.** Add the injectivity-preserving path
  independently; preserve all reference guards.
- **Slice D — actual-GPU comparison.** Emit/validate the dedicated result with
  the CPU-reference baseline and, for adopted optimized SLIM only, an
  additional comparison before a speed claim.

## Right-sizing
- One runtime adapter and one existing JobService GPU-queue participant are
  justified by the layer/thread seam. Do not add per-strategy queues, a backend
  registry, or a second sparse-GPU framework.

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
- [ ] Keep every local/global iteration on-device. In particular, implement
      SLIM energy, signed-area, and accepted-step reductions through
      `ComputeParallelPrimitives`; no per-iteration CPU readback may decide
      injectivity or line-search acceptance.
- [ ] Add the runtime/config backend request and requested/actual/fallback
      result telemetry; gate on `IDevice::IsOperational()` and fall back to
      `cpu_reference` when unavailable. Reject a strategy/GPU pair that missed
      parity during config preview rather than substituting a strategy.
- [ ] Preserve determinism within the documented GPU parity tolerance; preserve SLIM injectivity on the GPU path.

## Tests
- [ ] Opt-in `tests/integration/runtime/Test.ParameterizationGpuBackendGpuSmoke.cpp` labeled `gpu;vulkan` (mirroring `Test.KMeansGpuBackendGpuSmoke.cpp` — the adapter under test is runtime-owned): on a Vulkan-capable host the GPU ARAP/SLIM result matches the CPU reference within the documented parity tolerance, with zero flips for SLIM.
- [ ] Fallback: on the Null/non-operational device the runtime GPU request
      reports `ActualBackend == cpu_reference` and
      `FellBackToCPU == true` (default gate).
- [ ] Adoption: evaluate ARAP and SLIM independently. A parity miss is recorded
      as negative evidence and leaves that strategy's GPU request unavailable;
      it does not block a passing strategy.
- [ ] Determinism within tolerance across two GPU runs.
- [ ] Freeze per-strategy UV/energy parity, SLIM signed-area/injectivity, and
      fallback tolerances before implementation; assert the backend was
      operational rather than skipped/substituted.

## Docs
- [ ] GPU-vs-CPU manifest
      `benchmarks/geometry/manifests/parameterization_gpu_vs_cpu_smoke.yaml`
      with stable ID `geometry.parameterization.gpu_vs_cpu.smoke`, a stable
      built-in ARAP/SLIM dataset, `intent: gpu`, explicit warmup/measured
      counts, a CPU-reference same-fixture baseline, an optional optimized
      same-fixture comparison only where METHOD-025 exposed it, and metrics
      `runtime_ms`, `gpu_time_ms`, and `quality_error_l2`.
- [ ] Add `IntrinsicParameterizationGpuBenchmarkSmoke`, emitting schema-valid
      actual-Vulkan result JSON with device/backend, strategy, CPU-reference
      deltas, an optional SLIM-only optimized delta when METHOD-025 exposed it,
      SLIM injectivity, timing source, iteration/readback, fallback, and status
      diagnostics.
- [ ] Update each ARAP/SLIM method README backend-status table with the actual
      per-strategy outcome and the parameterization roadmap; note GPU
      numerical-tolerance limitations. Add `gpu_vulkan_compute` to a manifest
      only for an adopted strategy.

## Acceptance criteria
- [ ] Both strategies are evaluated. Every exposed GPU strategy has
      CPU-reference parity cited from an actual `gpu;vulkan` run; exposed SLIM
      also preserves injectivity. A miss remains CPU-only with negative
      evidence.
- [ ] GPU requests fall back honestly on non-operational devices with asserted telemetry (default CPU gate).
- [ ] The GPU-vs-CPU benchmark validates and runs; layering holds (geometry RHI-free; the adapter lives in runtime).
- [ ] The emitted actual-GPU result validates; skipped/fallback execution
      cannot satisfy the operational/parity acceptance row.

## Verification
```bash
# CPU gate (fallback + contract)
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Parameterization' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Opt-in GPU parity (Vulkan-capable host)
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests IntrinsicParameterizationGpuBenchmarkSmoke
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Parameterization|IntrinsicParameterizationGpuBenchmarkSmoke' --timeout 180
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/IntrinsicParameterizationGpuBenchmarkSmoke --strict
python3 tools/agents/validate_method_manifests.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- No numeric change versus the reference beyond documented parity tolerance; no speedup claim without the baseline benchmark.
- No live GPU work on the poll thread; readback drains through `AsyncBufferReadback`, not `IDevice::ReadBuffer`.
- No RHI import into `src/geometry`; no private GPU primitive library.

## Maturity
- Target: `Operational` on Vulkan-capable hosts and `ParityProven` against the
  CPU reference for each adopted strategy (mirroring `METHOD-020`). Requires
  the `ci-vulkan` run cited in `Verification`; CPU-only hosts stop at honest
  fallback, and a parity miss remains CPU-only. The linear strategies record
  no GPU follow-up.
