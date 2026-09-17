---
id: METHOD-026
theme: I
depends_on:
  - METHOD-025
  - RUNTIME-176
  - UI-036
  - RUNTIME-194
  - RUNTIME-195
  - RUNTIME-202
  - RUNTIME-269
maturity_target: ParityProven
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources, method.engine-integration, runtime.editor-prepared-frame-locality]
---
# METHOD-026 — Parameterization family GPU (Vulkan compute) backend and parity

## Goal
- Evaluate `gpu_vulkan_compute` for both iterative parameterization strategies
  (ARAP and SLIM), executing the per-triangle local step and global solve on
  the GPU. Expose it per strategy only after an actual `gpu;vulkan` run proves
  CPU-reference parity (and SLIM injectivity), with a GPU-vs-CPU comparison
  result and honest CPU-reference fallback. Linear one-shot strategies remain
  outside this task; GEOM-090 separately assesses LSCM/harmonic/BFF candidates.

## Non-goals
- No new strategy or numeric change — the GPU path must match the `METHOD-021`/`METHOD-022` reference within a documented parity tolerance and preserve SLIM injectivity.
- No GPU acceleration of the linear one-shot strategies (LSCM/SCP/BFF) in this
  task. [GEOM-090](../geometry/GEOM-090-vulkan-one-shot-parameterization-assessment.md)
  owns the newly requested, evidence-gated LSCM/harmonic/BFF assessment. SCP is
  not part of that existing-method cohort. No speedup or lack of benefit is
  assumed without matched measurements.
- No new GPU primitive library — reuse the shared `Extrinsic.Graphics.ComputeParallelPrimitives` (GRAPHICS-108) and the runtime GPU-queue/readback substrate rather than private CUB-equivalents.

## Context
- Owner/layer: a private backend implementation of the typed parameterization
  operation in `src/runtime`, the layer allowed to import RHI. This task
  introduces the runtime GPU request and
  requested/actual/fallback telemetry for ARAP/SLIM; the geometry strategy
  variant stays RHI-free and carries no family-wide GPU token.
- GPU shape: the local step (per-triangle signed-SVD rotation fit) is
  embarrassingly parallel; the global step is a sparse SPD solve run as a GPU
  Jacobi/CG iteration (matching the reference within tolerance). SLIM energy,
  signed-area, and line-search reductions remain on-device throughout the
  bounded iteration. Results drain once through the `RUNTIME-195` multi-range
  transfer/readback operation, never through a
  per-iteration CPU round trip or device-wide `ReadBuffer` stall.
- Reuse the bounded sparse kernels owned by
  [RUNTIME-269](../runtime/RUNTIME-269-shared-vulkan-sparse-solve-kernels.md).
  This task retains ARAP/SLIM assembly, constraints, iteration control and
  injectivity checks; the shared solver is not a second method owner.
- Gating: reference parity (`METHOD-021`/`022`) and completion of the
  SLIM-only `METHOD-025` optimized-CPU evaluation must exist first. The CPU
  reference is always canonical; compare against `cpu_optimized` only for SLIM
  if METHOD-025's adoption gate passed. ARAP has no optimized CPU baseline.
  Renderer/runtime code gates on
  `RHI::IDevice::IsOperational()`; a GPU request on a non-operational device
  falls back to `cpu_reference` with honest telemetry.
- Config/UI: this task extends the config/result model delivered by retired
  `RUNTIME-176` and the panel delivered by retired `UI-036` with
  `gpu_vulkan_compute` after the implementation exists. The existing
  `Runtime.ParameterizationOperations`, including
  `EditorParameterizationPreparedFrame`, owns the typed operation. Extend it
  and keep implementation dependencies private to this family.

## Control surfaces
- Config/UI/Agent: add the runtime-owned `gpu_vulkan_compute` request for the
  supported iterative strategies and expose it through the existing validated
  config apply path; unavailable execution falls back honestly.

## Backends


- Backend axis: adds `gpu_vulkan_compute` with `gpu;vulkan` parity.
  `cpu_reference` is always the oracle and fallback; an adopted SLIM
  `cpu_optimized` path is an additional comparison, never the ARAP oracle.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Triangle mesh topology, float positions, and strategy-specific pins/boundary constraints; faces are semantic inputs. |
| Compatible entity sources | Existing editable mesh contract; validate canonical property references without widening to unsupported topology. |
| RuntimeModule | Extend `Runtime.ParameterizationOperations`; keep implementation and backend dependencies private to the family. |
| Config/agent | Existing parameterization preview/apply path gains only adopted strategy/backend choices and honest requested/actual/fallback results. |
| UI | Extend the existing parameterization panel and `EditorParameterizationPreparedFrame`, using shared panel support. |
| Publication | Existing validated UV publication/history path preserves unrelated properties and rejects stale work. |
| End-to-end tests | This task owns backend selection, config source parity, fallback, and result publication coverage; the tests below own numerical/backend evidence. |

## Slice plan
- **Slice A — private backend/fallback.** Extend the typed operation's
  record/submit/readback contract and prove honest Null-device fallback in the
  default gate.
- **Slice B — ARAP Vulkan parity.** Land and verify one iterative strategy on
  an operational device.
- **Slice C — SLIM Vulkan parity.** Add the injectivity-preserving path
  independently; preserve all reference guards.
- **Slice D — actual-GPU comparison.** Emit/validate the dedicated result with
  the CPU-reference baseline and, for adopted optimized SLIM only, an
  additional comparison before a speed claim.

## Right-sizing
- One private backend implementation and one participant on the canonical
  `JobService` are justified by the layer/thread seam. Do not export an
  adapter, add per-strategy queues, a backend registry, or a second sparse-GPU
  framework.

## Required changes
- [ ] Implement local-step/global-solve GPU recording as private state of the
      typed parameterization operation. Upload mesh topology/positions once,
      iterate on the GPU, and drain UVs through `RUNTIME-195`. Follow the
      current clustering pattern: a private module-owned backend and one
      `GpuQueueParticipantHandle`, without an exported queue or backend DTO.
- [ ] Register one private parameterization participant with `JobService` so
      work records inside the renderer frame context with no extra present; do
      not expose a parameterization job queue or queue DTO.
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
- [ ] Opt-in
      `tests/integration/runtime/Test.ParameterizationGpuBackendGpuSmoke.cpp`
      labeled `gpu;vulkan`: through the typed operation, the private GPU
      ARAP/SLIM path matches the CPU reference within the documented parity
      tolerance, with zero flips for SLIM.
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
- [ ] The GPU-vs-CPU benchmark validates and runs; layering holds (geometry is
      RHI-free and the private backend implementation lives in runtime).
- [ ] The emitted actual-GPU result validates; skipped/fallback execution
      cannot satisfy the operational/parity acceptance row.
- [ ] UI, config, and agent GPU requests extend the same existing typed
      operation and prepared frame; do not introduce a parallel apply route.

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
- No live GPU work on the poll thread; readback drains through `RUNTIME-195`,
  not retired `AsyncBufferReadback` or `IDevice::ReadBuffer`.
- No RHI import into `src/geometry`; no private GPU primitive library.
- No public parameterization backend adapter, feature job queue/DTO, or
  replacement Sandbox facade.

## Maturity
- Target: `Operational` on Vulkan-capable hosts and `ParityProven` against the
  CPU reference for each adopted strategy (mirroring `METHOD-020`). Requires
  the `ci-vulkan` run cited in `Verification`; CPU-only hosts stop at honest
  fallback, and a parity miss remains CPU-only. The linear strategies record
  no GPU follow-up.
