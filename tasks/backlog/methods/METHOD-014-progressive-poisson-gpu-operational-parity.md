---
id: METHOD-014
theme: I
depends_on:
  - METHOD-013
  - CORE-009
maturity_target: ParityProven
---
# METHOD-014 — Progressive Poisson GPU operational parity

## Goal
- Connect the progressive Poisson Vulkan-compute pass output to the METHOD-013
  readback parser/parity diagnostics and return public GPU results only when the
  output matches the METHOD-012 CPU reference within documented tolerance.

## Non-goals
- No change to METHOD-012 CPU reference semantics.
- No CUDA backend.
- No speedup claim without validated benchmark output and baseline comparison.

## Context
- Owning subsystem/layer: runtime owns the GPU execution seam; METHOD-012 remains
  the method-pure CPU reference and correctness oracle.
- METHOD-013 retired at `CPUContracted`: planning, shader assets, recording,
  upload/readback-copy ownership, parsed readback payloads, and CPU-reference
  parity diagnostics exist, but public execution still falls back to CPU.
- This task owns the `Operational` and `ParityProven` milestones for the
  progressive Poisson Vulkan backend.

## Control surfaces
- Config: registered app section
  `sandbox.progressive_poisson` field `backend`.
- Command/API: `SandboxEditorProgressivePoissonCommand::Config.Backend`.
- UI: retired `RUNTIME-136` already exposes requested backend and fallback
  readout.

## Backends
- Backend axis: `cpu_reference` vs `gpu_vulkan_compute`.
- Public result policy: report `ActualBackend == gpu_vulkan_compute` only when
  GPU execution succeeds and parity diagnostics pass; otherwise return the
  METHOD-012 CPU reference with explicit fallback reason.

## Slice plan
- **Slice A — public completion/fallback.** Connect the already-recordable
  METHOD-013 path to parsed results and prove honest CPU fallback in the default
  gate.
- **Slice B — Vulkan parity.** Run the real compute/readback path on the fixed
  METHOD-012 fixtures and reject GPU publication outside frozen tolerances.
- **Slice C — GPU evidence.** Add a dedicated actual-GPU smoke result and
  baseline comparison; do not infer a speedup from parity alone.

## Right-sizing
- Finish the single runtime path, parser, and diagnostics delivered by
  METHOD-013. Do not add a second adapter, backend registry, device service, or
  synchronous readback path.
- Add one dedicated GPU smoke runner because actual-device evidence cannot be
  emitted by the CPU smoke. Reuse the benchmark result schema and the fixed
  METHOD-012 fixtures rather than creating GPU-benchmark infrastructure.

## Required changes
- [ ] Complete the GPU-capable runtime overload so completed Vulkan pass output
      flows through the readback parser/parity diagnostics.
- [ ] Return public results with requested/actual backend identity and parity
      deltas.
- [ ] Implement CPU fallback for non-operational devices, GPU pass failure,
      readback failure, and parity mismatch.
- [ ] Finalize `method.yaml` backend identity and parity tolerance fields.
- [ ] Add dedicated manifest
      `benchmarks/geometry/manifests/progressive_poisson_gpu_vulkan_smoke.yaml`
      with stable ID `geometry.progressive_poisson.gpu_vulkan.smoke`, built-in
      METHOD-012 fixture/params, `intent: gpu`, explicit warmup/measured counts,
      `baseline_comparison: cpu_reference_same_fixture`, and allowed metrics
      `runtime_ms`, `gpu_time_ms`, and `quality_error_l2`.
- [ ] Add an `IntrinsicProgressivePoissonGpuBenchmarkSmoke` runner that emits
      schema-valid result JSON from actual Vulkan execution. Put per-level
      parity, readback/fallback, device, and timing-source details in
      diagnostics; a skipped/fallback run cannot support a GPU performance
      claim.

## Tests
- [ ] Default CPU/null fallback test asserting a Vulkan request returns the CPU
      result with `ActualBackend == CPU`.
- [ ] Opt-in `gpu;vulkan` parity tests asserting per-level counts, accepted order
      where deterministic, splat radii, and the Poisson guarantee against shared
      fixtures.
- [ ] Benchmark manifest and actual-GPU result validation for the dedicated
      stable ID.

## Docs
- [ ] Update `methods/geometry/progressive_poisson/README.md` and
      `docs/methods/` backend notes with GPU backend behavior, tolerance,
      parity diagnostics, and fallback behavior.
- [ ] Cross-link `docs/architecture/algorithm-variant-dispatch.md`.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module surfaces
      change.

## Acceptance criteria
- [ ] Operational Vulkan hosts return `ActualBackend == GPU` only after parity
      diagnostics pass.
- [ ] Null/non-operational hosts return METHOD-012 CPU output with explicit
      fallback reason.
- [ ] `gpu;vulkan` parity tests pass under `ci-vulkan`; default CPU fallback
      tests pass under the default gate.
- [ ] Benchmark manifests/results validate; no performance claim is made without
      baseline comparison.
- [ ] The GPU result records exact CPU-reference fixture/params, device/backend,
      warmup/sample policy, and parity delta; fallback/skipped evidence is never
      labeled as GPU execution.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests IntrinsicProgressivePoissonGpuBenchmarkSmoke
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'ProgressivePoisson|IntrinsicProgressivePoissonGpuBenchmarkSmoke' --timeout 180
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci-vulkan/benchmark-ctest/IntrinsicProgressivePoissonGpuBenchmarkSmoke --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- Changing CPU reference semantics to make the GPU path match.
- Introducing CUDA.
- Claiming speedups without a baseline comparison.

## Maturity
- Target: `ParityProven` on Vulkan-capable hosts; CPU fallback remains the
  intended endpoint on Null/non-operational hosts.
