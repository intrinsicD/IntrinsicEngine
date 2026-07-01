---
id: METHOD-013
theme: none
depends_on: [METHOD-012, GRAPHICS-108]
maturity_target: Operational
---
# METHOD-013 — Progressive Poisson-disk sampling: GPU (Vulkan compute) backend + parity

## Goal
- Add a Vulkan-compute GPU backend for the progressive Poisson-disk sampler that reproduces the METHOD-012 CPU reference result within a documented parity tolerance, using phase-parallel spatial hashing and the GRAPHICS-108 scan/compaction primitives, and reports backend identity plus parity deltas — auto-falling back to the CPU reference when no operational device is present.

## Non-goals
- No change to the CPU reference semantics (METHOD-012 is canonical truth).
- No interactive backend-toggle UI in this task; RUNTIME-136 owns the user-facing
  selector after this backend seam is stable. METHOD-013 may expose command/config
  fields and backend status readout needed by that follow-up.
- No figure export (RUNTIME-133).
- No CUDA path; the GPU backend is Vulkan compute only.

## Context
- Status: active as of 2026-06-30. METHOD-012 provides the CPU reference + parity contract, and GRAPHICS-108 now provides the reusable GPU scan/compaction primitives.
- Owning subsystem/layer: the GPU dispatch seam lives in `runtime` (it needs `RHI::IDevice`), following the `algorithm-variant-dispatch` / GEOM-052 shared CPU/GPU backend pattern: a CPU-only entry in the method/geometry layer plus a GPU-capable overload that takes `RHI::IDevice&`, returns a `Backend ActualBackend` diagnostic, and falls back to CPU on failure. Gate on `RHI::IDevice::IsOperational()`.
- The CUDA reference (`progressive_poisson.cu`) uses CUB stream compaction and per-level phase-parallel grid hashing with a load factor; the Vulkan port builds the per-level hash grid in a storage buffer, marks accepted points per phase, and compacts via GRAPHICS-108. Respect the hash `hash_load_factor` and `randomize_grid_origin` knobs from `SamplerConfig`.
- Slice A follows the local KMeans seam pattern: expose requested-vs-actual backend
  diagnostics first, keep the default CPU gate deterministic, and let a Vulkan
  request fail over to METHOD-012 until later slices make the GPU path operational.

## Control surfaces
- Config: `EngineConfig.sandbox.progressive_poisson.backend`.
- Command/API: `SandboxEditorProgressivePoissonCommand::Config.Backend`.
- UI: progressive-Poisson status readout only in Slice A; interactive backend
  selection remains RUNTIME-136.

## Backends
- Backend axis: `cpu_reference` vs `gpu_vulkan_compute`.
- Slice A reports requested/actual backend and CPU fallback reason; later slices
  replace the Vulkan request fallback with real GPU execution and parity deltas.

## Slice plan
- **Slice A (this slice).** Promote the task, add backend selection to config and
  command DTOs, report requested/actual backend plus fallback reason, and cover
  the non-operational Vulkan request in the default CPU gate. Closes
  `Scaffolded -> CPUContracted`.
- **Slice B (this slice).** Add Vulkan storage-buffer layouts, shader descriptors,
  shader assets, and fail-closed dispatch planning for per-level hash/accept
  passes.
- **Slice C.1 (this slice).** Add a recordable runtime dispatch seam that binds
  the build-cells and accept-phase kernels through `RHI::ICommandContext`,
  writes the BDA state record, and delegates accepted/remaining stream
  compaction to GRAPHICS-108. Public Sandbox behavior remains CPU fallback.
- **Slice C.2 (implemented).** Add upload/readback ownership for SoA positions,
  `order`/`level_offsets`/`splat_radii`, then route the GPU-capable runtime
  overload through the recorded passes while still falling back on pass failure.
- **Slice D.1 (this slice).** Parse GPU readback payloads for
  `order`/`level_offsets`/`splat_radii` and compare them against CPU-reference
  output with parity diagnostics. Public Sandbox behavior remains CPU fallback.
- **Slice D.2.** Add `gpu;vulkan` parity tests and the heavy/nightly benchmark
  metric extension needed for `Operational` and `ParityProven`.

## Continuation note

- Slice D.1 records the runtime-owned readback parser for `order`,
  `level_offsets`, and `splat_radii`, validates structural invariants, and
  compares GPU-shaped output against METHOD-012 reference output plus per-level
  Poisson guarantees.
- The public Sandbox command still returns METHOD-012 CPU reference output for
  `gpu_vulkan_compute` requests until operational Vulkan parity evidence lands.
- Next resume point: connect completed Vulkan command output to the parser,
  preserve CPU fallback on any GPU pass failure or parity miss, then add opt-in
  `gpu;vulkan` parity tests before claiming `Operational`.

## Required changes
- [x] Slice A: expose backend selection in config/command DTOs and return
      requested/actual backend diagnostics while preserving CPU reference output.
- [x] Slice A: document and test CPU fallback for a non-operational Vulkan request.
- [x] Slice B: add the runtime GPU planning module, storage-buffer layout,
      BDA state/push contracts, build-cells/accept-phase shader assets, and
      per-level build/accept/compaction dispatch planning over the GRAPHICS-108
      primitive plans without recording GPU execution yet.
- [x] Slice C.1: add a runtime recordable execution seam in
      `Extrinsic.Runtime.ProgressivePoissonGpuBackend`: resource handles,
      pipeline set, BDA state-record builder, command recorder, method dispatch
      barriers, and accepted/remaining GRAPHICS-108 stream-compaction
      delegation.
- [x] Slice C.1: upgrade
      `assets/shaders/progressive_poisson_accept_phase.comp` from a phase mask
      to conservative conflict-checked accept/carry flag generation over the
      per-level hash table.
- [x] Slice C.2: add upload/readback ownership for SoA positions,
      `order`/`level_offsets`/`splat_radii`, and route the GPU-capable runtime
      overload through the recorded passes.
- [x] Slice D.1: add parsed readback payloads for
      `order`/`level_offsets`/`splat_radii` plus CPU-reference parity
      diagnostics for accepted order, per-level offsets, splat radii, and
      Poisson guarantees.
- [ ] Complete the GPU-capable runtime overload so completed Vulkan pass output
      flows through the readback/parity diagnostics and returns a public result
      carrying `ActualBackend` and parity deltas.
- [ ] Implement CPU fallback: when `IsOperational()` is false or a GPU pass fails, return the METHOD-012 reference result with `ActualBackend == CPU`.
- [ ] Finalize `method.yaml` parity tolerance and backend-identity reporting
      once the operational Vulkan path is parity-proven.

## Tests
- [x] Slice A: extend config/control tests for `sandbox.progressive_poisson.backend`.
- [x] Slice A: add command fallback coverage asserting a Vulkan request reports
      actual CPU output and an explicit fallback reason in the default CPU gate.
- [x] Slice B: add default-gate runtime contract coverage for GPU buffer layout,
      per-level phase dispatch planning, GRAPHICS-108 compaction-plan handoff,
      pipeline descriptor paths, and planning-only CPU fallback status.
- [x] Slice C.1: add default-gate runtime contract coverage for state-record
      BDA mapping, record order, push constants, method buffer barriers,
      accepted/remaining compaction delegation, invalid-resource fallback, and
      non-operational CPU fallback.
- [x] Slice C.2: add default-gate runtime contract coverage for resource
      allocation, SoA position/key uploads, dispatch recording, readback-copy
      targets, and non-operational pre-allocation CPU fallback.
- [x] Slice D.1: add default-gate runtime contract coverage for readback
      parsing, invalid/duplicate accepted-index rejection, reference-match
      parity diagnostics, and CPU fallback recommendation on parity mismatch.
- [ ] Add `gpu;vulkan` parity tests (under `ci-vulkan`) asserting the GPU backend reproduces the CPU reference's per-level counts and the Poisson guarantee (`min_dist >= r_L`) on shared fixtures, within the documented tolerance; assert `ActualBackend == GPU` when a device is operational.
- [ ] Add a fallback test asserting that on the Null device the API returns the CPU result with `ActualBackend == CPU` (runs on the default CPU gate).
- [ ] Add or extend a benchmark manifest with a `gpu_time_ms` metric and a CPU-vs-GPU speedup diagnostic (heavy/nightly), with baseline comparison before any speedup claim.

## Docs
- [x] Slice A: document backend selection and fallback status in method/runtime/config docs.
- [x] Slice B: document the planning-only shader/layout seam and CPU fallback state.
- [x] Slice C.1: document the recordable Vulkan dispatch seam while preserving
      CPU fallback and deferring upload/readback parity.
- [x] Slice C.2: document runtime-owned upload/readback-copy ownership while
      preserving CPU fallback and deferring Vulkan parity.
- [x] Slice D.1: document parsed readback payloads and CPU-reference parity
      diagnostics while preserving CPU fallback and deferring operational Vulkan
      parity.
- [ ] Document the GPU backend, parity tolerance, and fallback behavior in the method `README.md` and `docs/methods/` backend notes; cross-link `docs/architecture/algorithm-variant-dispatch.md`.
- [x] Regenerate/check `docs/api/generated/module_inventory.md` after the
      Slice D.1 module surface change; re-validate the method manifest.

## Acceptance criteria
- [x] Slice A default-gate contract proves a Vulkan request falls back to the CPU
      reference with requested/actual backend diagnostics and unchanged sampled
      output.
- [x] Slice B pins the Vulkan planning contract and shader artifacts while
      continuing to report planning-only CPU fallback for `gpu_vulkan_compute`
      requests.
- [x] Slice C.1 records build/accept dispatches and GRAPHICS-108 compaction
      delegation through RHI command contracts while public execution still
      falls back to the CPU reference.
- [x] Slice C.2 records runtime-owned SoA input upload, pass recording, and
      readback-copy targets for `order`/`level_offsets`/`splat_radii` while
      public execution still falls back to the CPU reference.
- [x] Slice D.1 parses seeded GPU readback payloads, rejects malformed payloads,
      and recommends CPU fallback when GPU-shaped output misses the CPU
      reference or per-level Poisson guarantee.
- [ ] The GPU backend reproduces the CPU reference within the documented parity tolerance and preserves the Poisson guarantee on the tested datasets.
- [ ] Backend identity (`ActualBackend`) and parity deltas are reported; the API falls back to CPU cleanly on a non-operational device.
- [ ] `gpu;vulkan` parity tests pass under `ci-vulkan`; the CPU fallback test passes on the default gate.
- [ ] Method layering holds (CPU entry stays method/geometry-pure; GPU overload lives at the runtime seam); no CUDA introduced.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|RuntimeConfigControlFacade' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'ProgressivePoisson' --timeout 120
python3 tools/agents/validate_method_manifests.py --root methods --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Latest Slice A verification (2026-06-30):
- `cmake --build --preset ci --target IntrinsicCoreTests IntrinsicRuntimeContractTests`
- `cmake --build --preset ci --target IntrinsicGeometryMethodTests`
- `ctest --test-dir build/ci --output-on-failure -R 'CoreEngineConfigLoad\.(SerializesAndLoadsEveryBootField|InvalidFieldsFallBackWithDiagnostics)$' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `ctest --test-dir build/ci --output-on-failure -R 'ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `ctest --test-dir build/ci --output-on-failure -R 'RuntimeConfigControlFacade\.SandboxProgressivePoissonConfigIsHotApplied|SandboxEditorUi\.ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `python3 tools/agents/validate_method_manifests.py --root methods --strict`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `tools/ci/run_clean_workshop_review.sh . --strict` (automated rows passed; no follow-up findings)
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check`

Latest Slice B verification (2026-06-30):
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `ctest --test-dir build/ci --output-on-failure -R 'ProgressivePoissonGpuBackend|SandboxEditorUi\.ProgressivePoisson|RuntimeConfigControlFacade\.SandboxProgressivePoissonConfigIsHotApplied' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `cmake --build --preset ci --target IntrinsicGraphicsVulkanSmokeTests`
- `python3 tools/repo/check_shader_outputs.py --dir build/ci/bin/shaders --require progressive_poisson_build_cells.comp.spv --require progressive_poisson_accept_phase.comp.spv`
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `python3 tools/agents/validate_method_manifests.py --root methods --strict`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/agents/generate_session_brief.py --check`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check`
- `git diff --check`

Latest Slice C.1 verification (2026-06-30):
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicGraphicsVulkanSmokeTests`
- First focused build failed because `tests/contract/runtime/Test.ProgressivePoissonGpuBackend.cpp` used `RHI::BufferManager` without importing `Extrinsic.RHI.BufferManager`; fixed by importing the module and reran the same build successfully.
- `ctest --test-dir build/ci --output-on-failure -R 'ProgressivePoissonGpuBackend|SandboxEditorUi\.ProgressivePoisson|RuntimeConfigControlFacade\.SandboxProgressivePoissonConfigIsHotApplied|ProgressivePoissonReference' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `python3 tools/repo/check_shader_outputs.py --dir build/ci/bin/shaders --require progressive_poisson_build_cells.comp.spv --require progressive_poisson_accept_phase.comp.spv`
- `python3 tools/agents/validate_method_manifests.py --root methods --strict`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check`
- `cmake --build --preset ci --target IntrinsicTests`
- First full CPU CTest run timed out once in unrelated `CoreTasks.CounterEventHighFanInRandomizedSignalsResumeExactlyOnce`; the isolated test passed in 0.04s and the full CPU gate rerun passed.
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j"$(nproc)"`
- `cmake --preset ci-vulkan`
- `cmake --build --preset ci-vulkan --target IntrinsicTests`
- `python3 tools/repo/check_shader_outputs.py --dir build/ci-vulkan/bin/shaders --require progressive_poisson_build_cells.comp.spv --require progressive_poisson_accept_phase.comp.spv`
- `ctest --test-dir build/ci-vulkan --output-on-failure -R 'GpuSmoke|VulkanBootstrapSmoke' --timeout 120 -j"$(nproc)"` (31 passed, 1 runtime-skipped async-compute histogram smoke)

Latest Slice C.2 verification (2026-07-01):
- `vulkaninfo --summary` (host reports NVIDIA GeForce RTX 4090 plus additional Vulkan devices)
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `ctest --test-dir build/ci --output-on-failure -R 'ProgressivePoissonGpuBackend|SandboxEditorUi\.ProgressivePoisson|RuntimeConfigControlFacade\.SandboxProgressivePoissonConfigIsHotApplied|ProgressivePoissonReference' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
- `python3 tools/agents/validate_method_manifests.py --root methods --strict`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check`
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
- `git diff --check`
- `python3 tools/agents/generate_session_brief.py --check`
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j"$(nproc)"` (3498 passed)
- `cmake --preset ci-vulkan`
- `cmake --build --preset ci-vulkan --target IntrinsicTests`
- `python3 tools/repo/check_shader_outputs.py --dir build/ci-vulkan/bin/shaders --require progressive_poisson_build_cells.comp.spv --require progressive_poisson_accept_phase.comp.spv`
- `ctest --test-dir build/ci-vulkan --output-on-failure -R '^ComputeParallelPrimitivesGpuSmoke\.VulkanScanAndCompactionMatchCpuReference$' --timeout 120`
- `ctest --test-dir build/ci-vulkan --output-on-failure -R 'ProgressivePoissonGpuBackend|ComputeParallelPrimitivesGpuSmoke|BufferReadbackGpuSmoke|GpuTransferFacadeGpuSmoke|TextureReadbackGpuSmoke|GpuReadbackJobGpuSmoke|VulkanBootstrapSmoke' --timeout 120 -j1` (17 passed)

Latest Slice D.1 verification (2026-07-01):
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`
- `ctest --test-dir build/ci --output-on-failure -R 'ProgressivePoissonGpuBackend|SandboxEditorUi\.ProgressivePoisson|RuntimeConfigControlFacade\.SandboxProgressivePoissonConfigIsHotApplied|ProgressivePoissonReference' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` (49 passed)
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
- `python3 tools/agents/generate_session_brief.py`
- `python3 tools/agents/validate_method_manifests.py --root methods --strict`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root .`
- `python3 tools/repo/check_layering.py --root src --strict`
- `python3 tools/repo/check_test_layout.py --root . --strict`
- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check`
- `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
- `python3 tools/agents/generate_session_brief.py --check`
- `git diff --check`
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j"$(nproc)"` (3502 passed)
- `cmake --preset ci-vulkan`
- `cmake --build --preset ci-vulkan --target IntrinsicTests`
- `python3 tools/repo/check_shader_outputs.py --dir build/ci-vulkan/bin/shaders --require progressive_poisson_build_cells.comp.spv --require progressive_poisson_accept_phase.comp.spv`
- `ctest --test-dir build/ci-vulkan --output-on-failure -R 'ProgressivePoissonGpuBackend|ComputeParallelPrimitivesGpuSmoke|BufferReadbackGpuSmoke|GpuTransferFacadeGpuSmoke|TextureReadbackGpuSmoke|GpuReadbackJobGpuSmoke|VulkanBootstrapSmoke' --timeout 120 -j1` (21 passed)

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Changing the CPU reference semantics to make the GPU path "match".
- Introducing CUDA or leaking `Vk*` types through RHI.
- Claiming speedups without a baseline comparison.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; this task owns the GPU `Operational` and `ParityProven` milestones for the sampler (`Operational` owned by METHOD-013).
- Slice A closes at `CPUContracted`; `Operational` owned by `METHOD-013` later
  slices.
- Slice B remains `CPUContracted`: it pins the Vulkan planning and shader
  contracts but intentionally keeps execution disabled until Slice C/D parity
  evidence exists.
- Slice C.1 remains `CPUContracted`: it records dispatches and compaction
  delegation but still lacks upload/readback ownership and `gpu;vulkan` parity
  evidence.
- Slice C.2 remains `CPUContracted`: it records runtime-owned upload/readback
  copy ownership but still lacks parsed GPU results and `gpu;vulkan` parity
  evidence.
- Slice D.1 remains `CPUContracted`: it parses GPU-shaped readback payloads and
  reports CPU-reference parity diagnostics, but public execution still falls
  back to CPU until operational `gpu;vulkan` parity evidence exists.
- On non-Vulkan/Null hosts the documented endpoint is CPU fallback via METHOD-012; no separate follow-up is owed for that path.
