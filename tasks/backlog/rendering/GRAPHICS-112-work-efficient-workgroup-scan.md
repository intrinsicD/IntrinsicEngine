---
id: GRAPHICS-112
theme: B
depends_on: []
---
# GRAPHICS-112 — Work-efficient workgroup scan + uint32 overflow guard in ComputeParallelPrimitives

## Goal
- Replace the Hillis-Steele workgroup-local scan in `parallel_prefix_scan.comp`
  with a work-efficient scan (subgroup-intrinsic or Blelloch) to cut the
  per-block barrier count and shared-memory traffic, and guard the GPU scan's
  `uint32` accumulation against silent overflow so it matches the CPU reference's
  overflow handling.

## Non-goals
- No change to the public scan/compaction record API, scratch layout, or
  dispatch-plan contract — this is an internal shader-efficiency and
  correctness-hardening change.
- No change to the count→dispatch-args publication path.
- No move to a wider (64-bit) accumulator type as the default; the overflow guard
  matches existing CPU-reference semantics, and any type widening is a separate
  decision.

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

## Required changes
- [ ] Rewrite the workgroup-local scan in `parallel_prefix_scan.comp` to a
      work-efficient formulation (prefer `GL_KHR_shader_subgroup_arithmetic`
      subgroup scan + a per-subgroup fixup; fall back to Blelloch if subgroup ops
      are unavailable), reducing barriers per block.
- [ ] Add the same treatment to `parallel_scan_add_offsets.comp` only if it
      shares the inefficiency; otherwise leave it.
- [ ] Add a defined overflow guard to the GPU accumulation matching the CPU
      reference's behavior (saturate/flag), documented in the module.
- [ ] Keep the block-sum recursion, barrier placement between passes, and dispatch
      sizing semantically unchanged.

## Tests
- [ ] Default CPU-gate parity contract stays green (CPU reference unchanged).
- [ ] Opt-in `gpu;vulkan` scan/compaction smoke matches the CPU reference on
      existing fixtures after the rewrite (no behavioral regression).
- [ ] Add a large-sum fixture that exercises the overflow-guard path and asserts
      GPU and CPU reference agree on the guarded result.

## Docs
- [ ] Note the scan algorithm change and the overflow-guard semantics in
      `docs/architecture/compute-parallel-primitives.md`.
- [ ] Cross-link the audit Finding 3.

## Acceptance criteria
- [ ] Block scan uses a work-efficient formulation with fewer barriers per block;
      existing scan/compaction parity smokes still pass unchanged.
- [ ] GPU accumulation no longer wraps silently; guarded behavior matches the CPU
      reference and is covered by a test.
- [ ] Public API, scratch layout, and dispatch contract are unchanged.
- [ ] Default CPU gate green; layering holds.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ComputeParallelPrimitives' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
# Vulkan-capable host:
cmake --preset ci-vulkan
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
