---
id: METHOD-014
theme: none
depends_on:
  - METHOD-013
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
- Config: `EngineConfig.sandbox.progressive_poisson.backend`.
- Command/API: `SandboxEditorProgressivePoissonCommand::Config.Backend`.
- UI: retired `RUNTIME-136` already exposes requested backend and fallback
  readout.

## Backends
- Backend axis: `cpu_reference` vs `gpu_vulkan_compute`.
- Public result policy: report `ActualBackend == gpu_vulkan_compute` only when
  GPU execution succeeds and parity diagnostics pass; otherwise return the
  METHOD-012 CPU reference with explicit fallback reason.

## Required changes
- [ ] Complete the GPU-capable runtime overload so completed Vulkan pass output
      flows through the readback parser/parity diagnostics.
- [ ] Return public results with requested/actual backend identity and parity
      deltas.
- [ ] Implement CPU fallback for non-operational devices, GPU pass failure,
      readback failure, and parity mismatch.
- [ ] Finalize `method.yaml` backend identity and parity tolerance fields.
- [ ] Extend the benchmark manifest/result payload with GPU timing and
      CPU-vs-GPU diagnostics without claiming speedup until a baseline is cited.

## Tests
- [ ] Default CPU/null fallback test asserting a Vulkan request returns the CPU
      result with `ActualBackend == CPU`.
- [ ] Opt-in `gpu;vulkan` parity tests asserting per-level counts, accepted order
      where deterministic, splat radii, and the Poisson guarantee against shared
      fixtures.
- [ ] Benchmark manifest and result validation for the GPU metric extension.

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

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'ProgressivePoisson' --timeout 120
python3 tools/agents/validate_method_manifests.py --root methods --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Changing CPU reference semantics to make the GPU path match.
- Introducing CUDA.
- Claiming speedups without a baseline comparison.

## Maturity
- Target: `ParityProven` on Vulkan-capable hosts; CPU fallback remains the
  intended endpoint on Null/non-operational hosts.
