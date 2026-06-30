---
id: GRAPHICS-108
theme: none
depends_on: []
maturity_target: Operational
---
# GRAPHICS-108 — Reusable Vulkan compute parallel primitives (scan + compaction)

## Goal
- Provide reusable, tested Vulkan compute primitives — device-wide exclusive/inclusive prefix scan and stream compaction over storage buffers — behind a small `graphics` API, so GPU algorithms (the progressive Poisson-disk GPU backend in METHOD-013, future clustering/binning) have CUB-equivalent building blocks instead of re-implementing them per method.

## Non-goals
- No radix sort in this task (call out as a follow-up if METHOD-013 needs it).
- No ECS/runtime/asset-service knowledge in the compute primitives (graphics layering only).
- No method-specific kernels; this task delivers generic scan/compaction only.

## Context
- Status: retired 2026-06-30; owner/agent: Codex; branch: `main` local iteration.
- Final implementation commit: this retirement commit.
- Current slice: Slice D is complete; compacted-count readback/dispatch-indirect publication and determinism coverage landed, and the task is retired at `Operational`.
- Owning subsystem/layer: `graphics` (`graphics/rhi` + `graphics/vulkan`; gate on `RHI::IDevice::IsOperational()`, no `Vk*` types through RHI). Tests are `gpu;vulkan` opt-in.
- The engine already dispatches compute (`RHI.CommandContext` `Dispatch`/`DispatchIndirect`, Vulkan backend `vkCmdDispatch`), has storage buffers (`RHI.BufferManager`, `BufferUsage::Storage`), readback (`Runtime.GpuReadbackJob`), and atomic-counter / shared-reduction shader exemplars (`assets/shaders/instance_cull.comp`, `post_histogram.comp`, `hzb_build.comp`). What is **missing** is a reusable multi-pass prefix-scan and stream-compaction primitive — exactly the CUB operations the CUDA reference (`progressive_poisson.cu`) relies on for phase-parallel hashing.
- CUDA is legacy/off by default (`AGENTS.md` §5); this task uses **Vulkan compute**, not CUDA.

## Slice plan
- **Slice A (completed, CPUContracted).** Add `Extrinsic.Graphics.ComputeParallelPrimitives` with public scan/compaction DTOs, deterministic CPU reference functions, status/debug-name helpers, and GPU request validation that reports `DeviceUnavailable` on Null/non-operational devices and `UnsupportedInCurrentSlice` on operational devices until Vulkan dispatch exists. Add CPU/default-gate contract tests, docs, and module inventory.
- **Slice B (completed, CPUContracted).** Add scan/compaction compute shaders and backend-agnostic record planning for the multi-pass dispatch, including scratch sizing and barrier sequencing contracts.
- **Slice C (completed, ParityProven for the primitive smoke).** Wire RHI/Vulkan compute dispatch/readback smoke coverage and add opt-in `gpu;vulkan` parity tests against the Slice A CPU reference across the size classes.
- **Slice D (completed, Operational).** Integrated stream-compaction count publication as readback and dispatch-indirect argument buffer output for consumers, added determinism coverage, and retired after the promoted Vulkan parity gate remained green.

## Required changes
- [x] Add scan/compaction compute shaders under `assets/shaders/` (block-scan + block-sum scan + scatter), following the existing compute-shader and push-constant conventions.
- [x] Add backend-neutral dispatch planning for recursive scratch sizing, pass ordering, and `ShaderWrite` barrier publication.
- [x] Add a `graphics`-layer host API that records the multi-pass dispatch, allocates scratch via `BufferManager`, inserts the `ShaderWrite -> ShaderRead` barriers, supports `uint32` keys/flags, and publishes the compacted count to a caller-owned output-count buffer.
- [x] Add higher-level compacted-count readback and/or indirect-args buffer integration for consumers.
- [x] Provide a CPU reference (plain serial scan/compaction) co-located with the tests for parity comparison.
- [x] Fail closed when the device is not operational (`IsOperational()` false): the API reports unavailability so callers can fall back to a CPU path.

## Tests
- [x] Add `gpu;vulkan` parity tests under the `ci-vulkan` preset comparing GPU scan/compaction against the CPU reference on deterministic pseudo-random inputs (varied sizes incl. empty, single element, multi-block, all-kept, all-dropped).
- [x] Add default-gate CPU contract tests for scan, compaction, deterministic ordering, invalid shapes, and fail-closed GPU facade statuses.
- [x] Add default-gate CPU contract tests for recursive dispatch planning, scratch layout, shader output roles, and barrier sequencing.
- [x] Add a Vulkan determinism test: identical input yields identical compacted output and count.
- [x] Confirm the default CPU gate stays green (these tests are label-gated and excluded from it).

## Docs
- [x] Document the primitive API, scratch-buffer ownership, and barrier contract under `docs/architecture/` (rendering/RHI area) and link it from the algorithm-variant-dispatch / GPU-seam docs.
- [x] Document the Slice A CPU reference/fail-closed contract in `src/graphics/renderer/README.md` and `docs/architecture/algorithm-variant-dispatch.md`.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [x] Prefix scan and stream compaction run as Vulkan compute over storage buffers and match the CPU reference bit-for-bit on integer inputs across the tested size classes.
- [x] The API gates on `IsOperational()` and reports unavailability instead of crashing on the Null device.
- [x] `gpu;vulkan` parity/determinism tests pass under `ci-vulkan`; the default CPU gate remains green.
- [x] Graphics layering is preserved (no ECS/runtime/asset-service imports; no `Vk*` leakage through RHI).

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

Completed Slice A verification:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'ComputeParallelPrimitives' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/generate_session_brief.py --check
git diff --check
```

The full default CPU-supported CTest gate passed 3,436/3,436 tests for Slice A.

Completed Slice B verification:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'ComputeParallelPrimitives' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicGraphicsVulkanSmokeTests
python3 tools/repo/check_shader_outputs.py --dir build/ci/bin/shaders --require parallel_prefix_scan.comp.spv --require parallel_scan_add_offsets.comp.spv --require parallel_compact_by_flags.comp.spv
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
```

The full default CPU-supported CTest gate passed 3,441/3,441 tests for Slice B.

Completed Slice C verification:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci --output-on-failure -R 'ComputeParallelPrimitives' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'ComputeParallelPrimitivesGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ComputeParallelPrimitivesGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_shader_outputs.py --dir build/ci/bin/shaders --require parallel_prefix_scan.comp.spv --require parallel_scan_add_offsets.comp.spv --require parallel_compact_by_flags.comp.spv
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
git diff --check
```

The focused default-gate compute suite passed 36/36 tests for Slice C. The promoted Vulkan compute smoke passed 1/1 in both `build/ci` and `build/ci-vulkan`. The full default CPU-supported CTest gate passed 3,839/3,839 tests for Slice C.

Completed Slice D verification:

```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'ComputeParallelPrimitives' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci --output-on-failure -R 'ComputeParallelPrimitivesGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ComputeParallelPrimitivesGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120
python3 tools/agents/generate_session_brief.py
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_shader_outputs.py --dir build/ci/bin/shaders --require parallel_prefix_scan.comp.spv --require parallel_scan_add_offsets.comp.spv --require parallel_compact_by_flags.comp.spv --require parallel_count_to_dispatch_args.comp.spv
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
git diff --check
```

The focused default-gate compute suite passed 20/20 tests for Slice D. The Vulkan compute smoke passed 1/1 in both `build/ci` and `build/ci-vulkan` with count readback, dispatch-args publication, and determinism coverage. The full default CPU-supported CTest gate passed 3,447/3,447 tests for Slice D.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing CUDA; the seam is Vulkan compute.
- Leaking `Vk*` types through RHI or adding ECS/runtime/asset-service knowledge to graphics.
- Adding method-specific kernels in this generic-primitive task.

## Maturity
- Retired at `Operational` on Vulkan-capable hosts. On non-Vulkan/Null hosts the API reports unavailable and callers use their CPU reference.
- Radix sort, if required by METHOD-013, is an explicit follow-up and out of scope here.
