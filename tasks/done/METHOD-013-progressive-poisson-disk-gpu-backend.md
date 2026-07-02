---
id: METHOD-013
theme: none
depends_on: [METHOD-012, GRAPHICS-108]
maturity_target: CPUContracted
completed_on: 2026-07-02
---
# METHOD-013 — Progressive Poisson-disk sampling: GPU backend contract slices

## Goal
- Add the progressive Poisson-disk Vulkan-compute backend contract through the
  runtime seam up to parsed GPU-shaped readback payloads and CPU-reference parity
  diagnostics while preserving deterministic CPU fallback for public Sandbox
  behavior.

## Non-goals
- No change to METHOD-012 CPU reference semantics.
- No public GPU result claim or speedup claim in this retired slice.
- No CUDA path.
- No interactive backend-toggle UI; retired `RUNTIME-136` owns the current
  selector surface and consumes requested-vs-actual fallback telemetry.

## Context
- Status: completed 2026-07-02 at `CPUContracted`.
- Owning subsystem/layer: CPU reference remains method-pure; GPU-capable
  dispatch/record/readback contracts live in `runtime` because they require
  `RHI::IDevice`, command contexts, GPU buffers, and readback ownership.
- METHOD-012 remains the canonical correctness oracle. This task added the
  Vulkan planning, shader assets, command recording, upload/readback ownership,
  and readback parser/parity diagnostics needed before an operational GPU result
  can be returned.
- The remaining operational/parity work is now tracked by `METHOD-014`, which
  must connect completed Vulkan output to the parser, return `ActualBackend ==
  GPU` only on parity success, add `gpu;vulkan` parity tests, and extend the
  benchmark manifest before any speedup claim.

## Control surfaces
- Config: `EngineConfig.sandbox.progressive_poisson.backend`.
- Command/API: `SandboxEditorProgressivePoissonCommand::Config.Backend`.
- UI: retired `RUNTIME-136` exposes the requested backend selector and fallback
  readout.

## Backends
- Backend axis: `cpu_reference` vs `gpu_vulkan_compute`.
- Reached state: Vulkan requests can plan/record GPU-shaped work and parse
  seeded readbacks, but public execution still falls back to the METHOD-012 CPU
  reference until `METHOD-014` proves operational parity.

## Required changes
- [x] Expose backend selection in config/command DTOs and return
      requested/actual backend diagnostics while preserving CPU reference output.
- [x] Document and test CPU fallback for non-operational Vulkan requests.
- [x] Add runtime GPU planning for storage-buffer layout, BDA state/push
      contracts, build-cells/accept-phase shader assets, per-level
      build/accept/compaction dispatch planning, and GRAPHICS-108 primitive
      handoff.
- [x] Add runtime recordable execution seam: resource handles, pipeline set,
      BDA state-record builder, command recorder, method dispatch barriers, and
      accepted/remaining stream-compaction delegation.
- [x] Add upload/readback ownership for SoA positions, `order`,
      `level_offsets`, and `splat_radii`, then route the GPU-capable overload
      through the recorded passes while still falling back on pass failure.
- [x] Add parsed readback payloads plus CPU-reference parity diagnostics for
      accepted order, per-level offsets, splat radii, and Poisson guarantees.
- [x] Move the remaining public GPU execution/parity return path to
      `METHOD-014`.

## Tests
- [x] Extend config/control tests for `sandbox.progressive_poisson.backend`.
- [x] Add command fallback coverage asserting Vulkan requests report actual CPU
      output and explicit fallback reasons in the default CPU gate.
- [x] Add default-gate runtime contract coverage for GPU buffer layout,
      dispatch planning, GRAPHICS-108 compaction handoff, pipeline descriptors,
      command recording, resource allocation, upload/readback-copy targets, and
      non-operational CPU fallback.
- [x] Add default-gate runtime contract coverage for readback parsing,
      malformed/duplicate accepted-index rejection, reference-match parity
      diagnostics, and CPU fallback recommendation on parity mismatch.
- [x] Leave real `gpu;vulkan` parity tests and benchmark metric extension to
      `METHOD-014`.

## Docs
- [x] Document backend selection and fallback status in method/runtime/config
      docs.
- [x] Document the planning, recordable dispatch, upload/readback-copy, parsed
      readback, and parity-diagnostic seams while preserving CPU fallback.
- [x] Regenerate/check `docs/api/generated/module_inventory.md` after module
      surface changes.
- [x] Re-validate the method manifest.
- [x] Open `METHOD-014` for operational GPU return, parity tests, benchmark
      metrics, and final method-doc tolerance updates.

## Acceptance criteria
- [x] A Vulkan request falls back to CPU with requested/actual diagnostics and
      unchanged sampled output in the default gate.
- [x] Vulkan planning, shader assets, command recording, upload/readback-copy,
      and parsed readback contracts are pinned under CPU/default-gate tests.
- [x] GPU-shaped readbacks are compared against METHOD-012 CPU reference output
      and recommend CPU fallback on malformed data, parity mismatch, or Poisson
      guarantee failure.
- [x] Method layering holds: method CPU entry stays pure; GPU overload remains
      at the runtime seam; no CUDA or `Vk*` leakage is introduced.
- [x] `METHOD-014` owns `Operational` / `ParityProven` public GPU execution.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|RuntimeConfigControlFacade' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_method_manifests.py --root methods --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Completed 2026-07-02 at `CPUContracted`.
Commit reference: this commit.

Latest recorded slice evidence before retirement included focused
`ProgressivePoissonGpuBackend`, Sandbox/config fallback tests, method manifest
validation, module inventory checks, default CPU gate runs, and opt-in Vulkan
smokes for the supporting compute/readback surfaces. This retirement commit
re-runs the method/task/documentation structural checks.

## Forbidden changes
- Changing CPU reference semantics to make the GPU path match.
- Introducing CUDA or leaking `Vk*` types through RHI/runtime APIs.
- Claiming GPU speedups or `ActualBackend == GPU` without `METHOD-014` parity
  evidence.

## Maturity
- Target reached: `CPUContracted`.
- `Operational` and `ParityProven` GPU execution are owned by `METHOD-014`.
- On non-Vulkan/Null hosts the intended endpoint remains documented CPU
  fallback via METHOD-012; no additional follow-up is owed for that path.
