---
id: GRAPHICS-108
theme: none
depends_on: []
---
# GRAPHICS-108 — Reusable Vulkan compute parallel primitives (scan + compaction)

## Goal
- Provide reusable, tested Vulkan compute primitives — device-wide exclusive/inclusive prefix scan and stream compaction over storage buffers — behind a small `graphics` API, so GPU algorithms (the progressive Poisson-disk GPU backend in METHOD-013, future clustering/binning) have CUB-equivalent building blocks instead of re-implementing them per method.

## Non-goals
- No radix sort in this task (call out as a follow-up if METHOD-013 needs it).
- No ECS/runtime/asset-service knowledge in the compute primitives (graphics layering only).
- No method-specific kernels; this task delivers generic scan/compaction only.

## Context
- Status: backlog.
- Owning subsystem/layer: `graphics` (`graphics/rhi` + `graphics/vulkan`; gate on `RHI::IDevice::IsOperational()`, no `Vk*` types through RHI). Tests are `gpu;vulkan` opt-in.
- The engine already dispatches compute (`RHI.CommandContext` `Dispatch`/`DispatchIndirect`, Vulkan backend `vkCmdDispatch`), has storage buffers (`RHI.BufferManager`, `BufferUsage::Storage`), readback (`Runtime.GpuReadbackJob`), and atomic-counter / shared-reduction shader exemplars (`assets/shaders/instance_cull.comp`, `post_histogram.comp`, `hzb_build.comp`). What is **missing** is a reusable multi-pass prefix-scan and stream-compaction primitive — exactly the CUB operations the CUDA reference (`progressive_poisson.cu`) relies on for phase-parallel hashing.
- CUDA is legacy/off by default (`AGENTS.md` §5); this task uses **Vulkan compute**, not CUDA.

## Required changes
- [ ] Add scan/compaction compute shaders under `assets/shaders/` (block-scan + block-sum scan + scatter), following the existing compute-shader and push-constant conventions.
- [ ] Add a `graphics`-layer host API that records the multi-pass dispatch (allocates scratch via `BufferManager`, inserts the `ShaderWrite -> ShaderRead` barriers, supports `uint32` keys/flags) and returns the compacted count via readback or an indirect-args buffer.
- [ ] Provide a CPU reference (plain serial scan/compaction) co-located with the tests for parity comparison.
- [ ] Fail closed when the device is not operational (`IsOperational()` false): the API reports unavailability so callers can fall back to a CPU path.

## Tests
- [ ] Add `gpu;vulkan` parity tests under the `ci-vulkan` preset comparing GPU scan/compaction against the CPU reference on randomized inputs (varied sizes incl. empty, single element, multi-block, all-kept, all-dropped).
- [ ] Add a determinism test: identical input yields identical compacted output and count.
- [ ] Confirm the default CPU gate stays green (these tests are label-gated and excluded from it).

## Docs
- [ ] Document the primitive API, scratch-buffer ownership, and barrier contract under `docs/architecture/` (rendering/RHI area) and link it from the algorithm-variant-dispatch / GPU-seam docs.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [ ] Prefix scan and stream compaction run as Vulkan compute over storage buffers and match the CPU reference bit-for-bit on integer inputs across the tested size classes.
- [ ] The API gates on `IsOperational()` and reports unavailability instead of crashing on the Null device.
- [ ] `gpu;vulkan` parity/determinism tests pass under `ci-vulkan`; the default CPU gate remains green.
- [ ] Graphics layering is preserved (no ECS/runtime/asset-service imports; no `Vk*` leakage through RHI).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'Scan|Compaction' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing CUDA; the seam is Vulkan compute.
- Leaking `Vk*` types through RHI or adding ECS/runtime/asset-service knowledge to graphics.
- Adding method-specific kernels in this generic-primitive task.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; this task owns the `Operational` GPU-primitive milestone (`Operational` owned by GRAPHICS-108). On non-Vulkan/Null hosts the API reports unavailable and callers use their CPU reference.
- Radix sort, if required by METHOD-013, is an explicit follow-up and out of scope here.
