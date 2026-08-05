---
id: METHOD-020
theme: I
depends_on: [METHOD-019, RUNTIME-175, RUNTIME-194, RUNTIME-195, GEOM-075]
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner: "Codex-LOPVulkan"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-05T04:17:55Z"
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.support-radius-policy, method.engine-integration]
maturity_target: ParityProven
---
# METHOD-020 — LOP-family GPU (Vulkan compute) backend and parity

## Status
- Completed on 2026-08-05 at `ParityProven` for ordinary LOP and isotropic
  WLOP on the frozen v3 fixtures. Anisotropic WLOP, CLOP, and EAR remain
  explicit capability-negative Vulkan pairs. The original run-001 is retained
  as rejected harness evidence; the hardened protocol and accepted result use
  the separate v4/run-002 custody paths.
- Commit: pending final evidence binding.

## Goal
- Evaluate an explicit command-recording `gpu_vulkan_compute` path for every
  LOP-family consolidation strategy (WLOP/LOP/CLOP/EAR), exposing it only for
  strategies that match the CPU reference within frozen tolerance. Prove each
  adopted path with an opt-in `gpu;vulkan` parity smoke and a GPU benchmark
  that reports its CPU-reference baseline.

## Non-goals
- No algorithm/variant changes; the GPU path reproduces the reference numerics, it does not redefine them.
- No CUDA backend, CUDA build dependency, or CUDA/Vulkan interoperability path.
- No second config lane or editor panel. Extend the delivered
  `RUNTIME-175`/`UI-035` surfaces only after the GPU path passes parity.
- No synchronous device-wide readback or work on the platform poll thread;
  this task owns the private runtime backend implementation, canonical
  JobService scheduling/readback use, shaders, and parity evidence.
- No speedup claim without the benchmark baseline; no new public parameters.

## Context
- Owner/layer: `src/runtime` (RHI-allowed), following the `docs/architecture/algorithm-variant-dispatch.md` split — the CPU reference stays in `src/geometry` with no RHI, and the GPU-capable path lives where `Extrinsic::RHI::IDevice&` is allowed, gated on `IDevice::IsOperational()` with honest `ActualBackend`/`FellBackToCPU` telemetry.
- Reuse `Extrinsic.Graphics.ComputeParallelPrimitives` (`GRAPHICS-108`) for
  scan/compaction rather than private primitives, and drain results through the
  `RUNTIME-195` multi-range transfer/readback operation. The old public
  K-Means backend/queue family is explicitly not a template; `RUNTIME-196`
  retires it.
- The projection iteration (neighbor-weighted attraction + repulsion, or the CLOP continuous per-component term) maps to a compute dispatch over the projected set; the shared weight math from `Geometry.PointCloud.Kernels` is reproduced in the shader with the same closed forms the CPU path uses.
- `GEOM-075` resolves Auto/Manual intent to one positive world-unit support
  radius and rejects unsafe predicted work before backend selection. Vulkan
  uses that fixed radius with an `h`-sized count/scan/scatter cell grid: source
  cells are built once, projected cells are rebuilt per iteration, 27 adjacent
  cells are exact-distance filtered, and neighbor contributions are streamed
  without a global pair list.
- Verification requires the `ci-vulkan` preset and a Vulkan-capable host; on non-operational devices the path must fall back to the CPU reference with honest telemetry, and the parity test asserts that fallback.
- `METHOD-019` is an evidence-ordering gate, not a promise that every
  `cpu_optimized` strategy survived. Compare every GPU strategy against
  `cpu_reference`; include an optimized CPU comparison only where METHOD-019
  actually exposed that strategy.

## Control surfaces
- Config/UI/Agent: extend the `RUNTIME-175` typed runtime/app backend request with
  `gpu_vulkan_compute` only when this implementation exists, through the
  validated `RUNTIME-175` config/operation and `UI-035` presentation path. The
  geometry-owned CPU selector remains RHI-free.

## Backends
- Backend axis: adds `gpu_vulkan_compute`; `cpu_reference` stays the parity oracle and the fallback target.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | A finite contiguous `vec3` position property/span, plus an optional count-matched finite `vec3` normal property for anisotropic WLOP/EAR. |
| Compatible entity sources | Every resolved point-cloud, graph, or mesh vertex/halfedge/edge/face property domain satisfying the existing consolidation preflight; topology provenance is not an eligibility filter. |
| `RuntimeModule` | Extend the existing private execution state in `Extrinsic.Runtime.PointCloudConsolidationModule`; CPU remains the reference/fallback and Vulkan records through operational RHI only. |
| Config/agent | Add `gpu_vulkan_compute` to the existing schema-versioned config and typed operation only after an implementation exists; use the same preview/validate/apply path as UI. |
| UI | Extend the existing property-aware panel backend selector for every compatible domain; unsupported strategy/backend pairs fail preview. |
| Publication | Reuse the existing undoable same-domain property publication and topology-free point-cloud replacement rules; GPU results never bypass canonical writeback. |
| End-to-end tests | Null-device fallback/control-source parity in the CPU gate plus operational `gpu;vulkan` CPU-reference parity for each exposed strategy and compatible source fixture. |

## Slice plan
- **Slice A — private backend/fallback.** Extend the existing typed operation
  with explicit record/submit/readback behavior and prove Null-device fallback
  in the default CPU gate.
- **Slice B — one-strategy Vulkan parity.** Land WLOP/LOP first with actual
  operational-device evidence and frozen tolerance.
- **Slice C — CLOP/EAR parity.** Add each kernel path independently; reuse only
  already-landed shared GPU primitives.
- **Slice D — actual-GPU benchmark.** Emit and validate the dedicated result
  with CPU-reference baseline/device identity before any speed claim.

## Right-sizing
- One private backend implementation is justified by the CPU/GPU layer seam.
  Keep strategy dispatch inside the `RUNTIME-175` feature operation; do not
  export a backend adapter, registry, or per-strategy service.
- Register one private participant with `JobService`; do not create a second
  queue, service, or synchronous device-wide readback path.

## Required changes
- [x] Implement GPU projection as private state/implementation of the
      `RUNTIME-175` typed consolidation operation, taking `RHI::IDevice&`,
      gating on `IsOperational()`, and reusing persistent buffers across
      iterations without exporting `Runtime.ConsolidationGpuBackend`.
- [x] Register the consolidation participant privately with `JobService`;
      submit through the real frame context and publish completed results
      through the delivered `RUNTIME-175` mutation/writeback path.
- [x] Add the compute shader assets for the adopted ordinary-LOP/isotropic-WLOP
      attraction, repulsion, density, convergence, and reduction passes under
      `assets/shaders/`, recorded through the RHI compute path. CLOP remains an
      explicit capability-negative pair rather than receiving a partial kernel.
- [x] Build bounded dense cell ranges with count, shared prefix scan, and scatter
      passes; guard cell count/occupancy/memory, build the source grid once,
      rebuild the projected grid per iteration, and exact-filter the 27-cell
      candidate set without materializing a global neighbor-pair list.
- [x] Reproduce the `Geometry.PointCloud.Kernels` weight/repulsion closed forms in-shader; document any float-precision divergence and bound it in the parity tolerance.
- [x] Keep every projection iteration and convergence reduction on-device;
      drain the final result once through the `RUNTIME-195` readback operation. No
      per-iteration CPU readback may steer convergence.
- [x] Honest fallback + telemetry: a `Backend::GPU` request on a null/non-operational device runs the CPU reference and reports `FellBackToCPU`.
- [x] Extend the delivered `RUNTIME-175` config/operation and `UI-035` panel with
      `gpu_vulkan_compute` only after the implementation exists; all UI,
      config-file, and agent requests use the same preview/validate/apply path.
- [x] Update a package `method.yaml` to list `gpu_vulkan_compute` only if at
      least one strategy it contains passes parity; add the GPU benchmark
      manifest under `benchmarks` for the evaluated family. Record the exact
      per-strategy capability matrix in package docs and result diagnostics.

## Tests
- [x] `tests/integration/runtime/Test.PointCloudConsolidationGpuParity.cpp`
      labeled `gpu;vulkan` (opt-in), asserting the typed operation's private GPU
      path matches the CPU reference within frozen per-strategy tolerances.
- [x] Fallback telemetry: `Backend::GPU` requested on a non-operational/null device reports `ActualBackend == cpu_reference` and `FellBackToCPU == true`, verified in the default CPU gate (no GPU required).
- [x] Config/control-surface parity: Editor, AgentCli, and Programmatic requests
      produce the same validated backend request; the panel never schedules
      GPU work from the poll thread. Unsupported strategy/backend pairs fail
      preview rather than silently running a different strategy.
- [x] Determinism within the documented tolerance across two GPU runs on the same host.

## Docs
- [x] GPU benchmark manifest
      `benchmarks/geometry/manifests/lop_family_gpu_vulkan_smoke_v3.yaml` with
      stable ID `geometry.lop_family.gpu_vulkan.v3.smoke`, a stable built-in
      dataset, `params.intent: gpu`, explicit warmup/measured counts,
      `baseline_comparison: cpu_reference_same_fixture`, and metrics
      `runtime_ms`/`gpu_time_ms` plus worst-of-parity-and-repeat
      `quality_error_l2`/`quality_error_linf`; backend identity and fallback
      remain fail-closed execution-status diagnostics. Exclude it from the
      default CPU smoke runner.
- [x] Add `IntrinsicLopFamilyGpuBenchmarkSmoke`, emitting schema-valid result
      JSON only from actual Vulkan execution with backend/device, strategy,
      CPU-reference parity, fallback, timing source, and iteration diagnostics.
- [x] Add an unsanitized optimized promoted-Vulkan benchmark preset separate
      from `ci-vulkan`; parity remains sanitizer-backed while performance
      evidence records release device and end-to-end phase timing.
- [x] Update each package README backend-status table (`gpu_vulkan_compute` → `METHOD-020`), the parity tolerance, and the shader/precision limitations.
- [x] Note the GPU backend and its host requirement in the `docs/architecture/algorithm-variant-dispatch.md` current-exemplar section if the family becomes a cited exemplar.

## Acceptance criteria
- [x] Every strategy is evaluated. Each exposed GPU strategy passes the
      `gpu;vulkan` parity smoke on a Vulkan-capable host and is cited in
      `Verification` as actually run; a miss stays CPU-only with recorded
      negative evidence.
- [x] Fallback telemetry is asserted in the default CPU gate.
- [x] The GPU benchmark validates and reports a CPU-reference baseline (no bare speedup claim).
- [x] Where METHOD-019 exposed `cpu_optimized`, report it as an additional
      same-fixture comparison; do not fabricate an optimized baseline for a
      strategy that failed its adoption gate.
- [x] The actual-GPU result validates; skipped/fallback execution is reported
      honestly and cannot satisfy the Vulkan acceptance row.
- [x] No `Vk*` types cross the public seam.

## Verification evidence
- The canonical CPU-supported receipt is
  `tasks/evidence/METHOD-020/commands/ci-tests.json` (4,091/4,091 selected
  cases passed), including Null-device fallback and control-surface parity.
- The isolated ASan receipt is
  `tasks/evidence/METHOD-020/commands/ci-asan-tests.json`; the promoted Vulkan
  parity and benchmark receipts are retained alongside the run-002 bundle.
- METHOD-019 adopted no `cpu_optimized` LOP-family backend, so the Vulkan
  benchmark reports the same-fixture CPU reference and explicitly makes no
  optimized-backend or speedup claim.
- Ordinary LOP and isotropic WLOP are the only exposed Vulkan pairs.
  Anisotropic WLOP, CLOP, and EAR fail the shared preview preflight with named
  capability diagnostics rather than substituting another strategy.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Consolidation' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests IntrinsicLopFamilyGpuBenchmarkSmoke
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Consolidation|IntrinsicLopFamilyGpuBenchmarkSmoke' --timeout 180
cmake --preset ci-vulkan-release
cmake --build --preset ci-vulkan-release --target IntrinsicLopFamilyGpuBenchmarkSmoke
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
- No private scan/compaction primitives (reuse `ComputeParallelPrimitives`); no
  `IDevice::ReadBuffer` or retired `AsyncBufferReadback` compatibility path for
  the drain (use the `RUNTIME-195` shared batch).
- No speedup claim without the baseline; no `Vk*` types on the public seam.
- No public consolidation backend module, feature queue, or parallel operation
  beside `RUNTIME-175`.

## Maturity
- Target: `Operational` on Vulkan-capable hosts and `ParityProven` against the
  CPU reference for every adopted strategy. Requires the `ci-vulkan` preset
  run cited in `Verification`; CPU-only hosts stop at the asserted honest
  fallback, and a parity miss remains CPU-only.
