---
id: METHOD-013
theme: none
depends_on: [METHOD-012, GRAPHICS-108]
---
# METHOD-013 — Progressive Poisson-disk sampling: GPU (Vulkan compute) backend + parity

## Goal
- Add a Vulkan-compute GPU backend for the progressive Poisson-disk sampler that reproduces the METHOD-012 CPU reference result within a documented parity tolerance, using phase-parallel spatial hashing and the GRAPHICS-108 scan/compaction primitives, and reports backend identity plus parity deltas — auto-falling back to the CPU reference when no operational device is present.

## Non-goals
- No change to the CPU reference semantics (METHOD-012 is canonical truth).
- No interactive UI wiring (RUNTIME-134) and no figure export (RUNTIME-133).
- No CUDA path; the GPU backend is Vulkan compute only.

## Context
- Status: backlog. Gated on METHOD-012 (reference + parity contract) and GRAPHICS-108 (GPU scan/compaction primitives).
- Owning subsystem/layer: the GPU dispatch seam lives in `runtime` (it needs `RHI::IDevice`), following the `algorithm-variant-dispatch` / GEOM-027 shared CPU/GPU backend pattern: a CPU-only entry in the method/geometry layer plus a GPU-capable overload that takes `RHI::IDevice&`, returns a `Backend ActualBackend` diagnostic, and falls back to CPU on failure. Gate on `RHI::IDevice::IsOperational()`.
- The CUDA reference (`progressive_poisson.cu`) uses CUB stream compaction and per-level phase-parallel grid hashing with a load factor; the Vulkan port builds the per-level hash grid in a storage buffer, marks accepted points per phase, and compacts via GRAPHICS-108. Respect the hash `hash_load_factor` and `randomize_grid_origin` knobs from `SamplerConfig`.

## Required changes
- [ ] Add per-level phase-parallel spatial-hash + accept compute shaders under `assets/shaders/` (cell hashing with configurable load factor; phase iteration so no two accepted points in a phase fall within `r_L`).
- [ ] Add the GPU-capable overload (runtime seam) that uploads SoA positions, runs the per-level build/accept/compact passes via GRAPHICS-108, reads back `order`/`level_offsets`/`splat_radii`, and returns a result carrying `ActualBackend` and parity diagnostics.
- [ ] Implement CPU fallback: when `IsOperational()` is false or a GPU pass fails, return the METHOD-012 reference result with `ActualBackend == CPU`.
- [ ] Update `method.yaml` backends to include `gpu_vulkan_compute`; record parity tolerance and backend-identity reporting.

## Tests
- [ ] Add `gpu;vulkan` parity tests (under `ci-vulkan`) asserting the GPU backend reproduces the CPU reference's per-level counts and the Poisson guarantee (`min_dist >= r_L`) on shared fixtures, within the documented tolerance; assert `ActualBackend == GPU` when a device is operational.
- [ ] Add a fallback test asserting that on the Null device the API returns the CPU result with `ActualBackend == CPU` (runs on the default CPU gate).
- [ ] Add or extend a benchmark manifest with a `gpu_time_ms` metric and a CPU-vs-GPU speedup diagnostic (heavy/nightly), with baseline comparison before any speedup claim.

## Docs
- [ ] Document the GPU backend, parity tolerance, and fallback behavior in the method `README.md` and `docs/methods/` backend notes; cross-link `docs/architecture/algorithm-variant-dispatch.md`.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change; re-validate the method manifest.

## Acceptance criteria
- [ ] The GPU backend reproduces the CPU reference within the documented parity tolerance and preserves the Poisson guarantee on the tested datasets.
- [ ] Backend identity (`ActualBackend`) and parity deltas are reported; the API falls back to CPU cleanly on a non-operational device.
- [ ] `gpu;vulkan` parity tests pass under `ci-vulkan`; the CPU fallback test passes on the default gate.
- [ ] Method layering holds (CPU entry stays method/geometry-pure; GPU overload lives at the runtime seam); no CUDA introduced.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ProgressivePoisson' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'ProgressivePoisson' --timeout 120
python3 tools/agents/validate_method_manifests.py --root methods --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Changing the CPU reference semantics to make the GPU path "match".
- Introducing CUDA or leaking `Vk*` types through RHI.
- Claiming speedups without a baseline comparison.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; this task owns the GPU `Operational` and `ParityProven` milestones for the sampler (`Operational` owned by METHOD-013).
- On non-Vulkan/Null hosts the documented endpoint is CPU fallback via METHOD-012; no separate follow-up is owed for that path.
