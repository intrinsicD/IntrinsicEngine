---
id: GRAPHICS-112
theme: B
depends_on: []
completed: 2026-07-04
---
# GRAPHICS-112 — Work-efficient workgroup scan + uint32 overflow guard in ComputeParallelPrimitives

## Completion
- Completed: 2026-07-04. Commit/PR: pending current change.
- Maturity: `Operational` on promoted Vulkan hosts with subgroup arithmetic;
  `CPUContracted` elsewhere.
- Summary: replaced the workgroup-wide Hillis-Steele scan with a subgroup scan
  plus shared per-subgroup fixup, added `UINT32_MAX` saturation guards to scan
  and add-offset accumulation, and extended the Vulkan smoke with local and
  multiblock overflow fixtures.

## Goal
- Replace the Hillis-Steele workgroup-local scan in `parallel_prefix_scan.comp`
  with a work-efficient scan (subgroup-intrinsic or Blelloch) to cut the
  per-block barrier count and shared-memory traffic, and guard the GPU scan's
  `uint32` accumulation against silent overflow. CPU keeps reporting
  `SumOverflow`; GPU clamps to `UINT32_MAX` because the record API has no
  dispatch-time status channel.

## Non-goals
- No change to the public scan/compaction record API, scratch layout, or
  dispatch-plan contract — this is an internal shader-efficiency and
  correctness-hardening change.
- No change to the count→dispatch-args publication path.
- No move to a wider (64-bit) accumulator type as the default; the overflow guard
  matches the CPU reference's no-wrap contract by saturating on GPU, and any
  type widening is a separate decision.
- No runtime shader-variant fallback for Vulkan devices lacking subgroup
  arithmetic; promoted Vulkan operational smoke is the capability gate.

## Context
- Owning subsystem/layer: `graphics/renderer`
  (`Extrinsic.Graphics.ComputeParallelPrimitives` + `assets/shaders/parallel_*.comp`).
- Origin: `docs/reviews/2026-07-01-gpu-geometry-backend-io-audit.md` Finding 3.
  The 256-lane block scan is Hillis-Steele with two `barrier()`s per step
  (16 barriers/block) and O(n log n) work (`parallel_prefix_scan.comp:74-80`); a
  subgroup scan (`subgroupInclusive/ExclusiveAdd`) or double-buffered Blelloch
  roughly halves the barriers and shared traffic. Separately, the CPU reference
  guards accumulation overflow (`Graphics.ComputeParallelPrimitives.cpp:617-623`)
  while the GPU `uint32` path can wrap silently.
- Behavior-preserving: existing scan/compaction parity smokes and the CPU
  reference are the correctness oracle; results must be identical (modulo the
  documented overflow-guard behavior) after the change.
- Overflow semantics: CPU scan returns `SumOverflow` before wrapping. GPU scan
  and add-offset passes saturate at `UINT32_MAX` so downstream GPU consumers see
  a stable non-wrapped value without changing the public record API.

## Required changes
- [x] Rewrite the workgroup-local scan in `parallel_prefix_scan.comp` to a
      subgroup scan plus shared per-subgroup fixup, reducing the shared-memory
      scan from 256 lanes to the subgroup-count tail.
- [x] Leave `parallel_scan_add_offsets.comp` as a simple elementwise add pass,
      but add the same saturation guard to its offset accumulation.
- [x] Add a defined GPU overflow guard: scan outputs, block sums, and add-offset
      results saturate to `UINT32_MAX`; CPU still reports `SumOverflow`.
- [x] Keep the block-sum recursion, barrier placement between passes, and dispatch
      sizing semantically unchanged.

## Tests
- [x] Default CPU-gate parity contract stays green (CPU reference unchanged).
- [x] Opt-in `gpu;vulkan` scan/compaction smoke matches the CPU reference on
      existing fixtures after the rewrite (no behavioral regression).
- [x] Add local and multiblock overflow fixtures that assert CPU reports
      `SumOverflow` and GPU saturates instead of wrapping.

## Docs
- [x] Note the scan algorithm change and the overflow-guard semantics in
      `docs/architecture/compute-parallel-primitives.md`.
- [x] Cross-link the audit Finding 3.

## Acceptance criteria
- [x] Block scan uses a work-efficient subgroup formulation;
      existing scan/compaction parity smokes still pass unchanged.
- [x] GPU accumulation no longer wraps silently; CPU reports `SumOverflow`, GPU
      saturates to `UINT32_MAX`, and both paths are covered by tests.
- [x] Public API, scratch layout, and dispatch contract are unchanged.
- [x] Default CPU gate green; layering holds.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ComputeParallelPrimitives' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
git grep -n "\\[DBG-" -- .
tools/ci/run_clean_workshop_review.sh . --strict
# Vulkan-capable host:
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests_Shaders
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests
cmake --build --preset ci-vulkan --target IntrinsicTests
python3 tools/repo/check_shader_outputs.py --dir build/ci-vulkan/bin/shaders --require parallel_prefix_scan.comp.spv
ctest --test-dir build/ci-vulkan --output-on-failure -R 'ComputeParallelPrimitivesGpuSmoke' -L 'gpu' --timeout 120
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Changing the public scan/compaction API or scratch layout.
- Regressing scan/compaction parity against the CPU reference.

## Maturity
- Target: `Operational` on Vulkan-capable hosts (existing `gpu;vulkan` scan
  smoke re-proves parity post-rewrite); `CPUContracted` everywhere else. No new
  CPU-reference semantics are introduced beyond the documented overflow guard.
