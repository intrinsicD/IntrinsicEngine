# GRAPHICS-040B — IReconstructor interface + reference TAA

## Goal
- Lock the vendor-free `IReconstructor` CPU-public interface and implement the in-engine
  reference TAA (Karis-style temporal resolve, 5×5 YCoCg variance clip, exposure-aware
  weighting, reset-driven history invalidate) with the retained ping-pong history buffer
  (`GRAPHICS-040` decisions 3/4/5/8), with `contract;graphics` tests.

## Non-goals
- No recipe selection / post-chain integration (that is `GRAPHICS-040C`).
- No vendor backends (deferred per-vendor children, not opened).

## Context
- Owner layer: `graphics/renderer` (interface + reference TAA + history resource).
- Depends on `GRAPHICS-040A` (jitter + motion vectors).
- Decision 4: `Apply(JitteredColor, Depth, MotionVectors, HistoryColor, Output,
  ReconstructionHints) -> ReconstructionResult`; hints carry `Sharpness`, `Exposure`,
  `JitterOffset`, `FrameIndex`, `InputExtent`, `OutputExtent`, `Reset`; result reports
  `Applied`, `DisocclusionPercent`, fail-closed reason. No vendor/`Vk*`/SDK types in the surface.
- Decision 3: retained `R16G16B16A16_SFLOAT` ping-pong history with a `framesInFlight`
  retire deadline (mirroring the SMAA retained-resource discipline).
- Decision 5: the reference TAA is the only concrete `IReconstructor` in this roadmap.
  Decision 8: disocclusion falls back to neighborhood-clamped current color; the fraction
  is reported via `DisocclusionPercent`.

## Required changes
- [ ] Add the vendor-free `IReconstructor` interface + `ReconstructionHints`/`ReconstructionResult`.
- [ ] Add the retained `R16G16B16A16_SFLOAT` ping-pong history pair with retire deadlines.
- [ ] Implement the reference TAA resolve (5×5 YCoCg variance clip, exposure weighting,
      reset invalidate, disocclusion clamp fallback).
- [ ] `contract;graphics` tests for the interface contract, history lifetime, and the
      disocclusion fallback (CPU-evaluable on a small synthetic input).

## Tests
- [ ] `contract;graphics` — interface fail-closed reasons; history ping-pong/retire
      lifetime; reset invalidate; disocclusion fraction reporting.
- [ ] CPU gate green.

## Docs
- [ ] Document `IReconstructor` + the reference TAA in `src/graphics/renderer/README.md`.
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] The interface is vendor-free; the reference TAA + history resource exist and are CPU-tested.
- [ ] No vendor/`Vk*`/SDK types leak into the public surface.
- [ ] No new layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Vendor/`Vk*`/SDK types in the `IReconstructor` public surface.
- Importing a vendor SDK into promoted `graphics`.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` under the null RHI for the interface + reference-TAA contract.
- `Operational` owned by `GRAPHICS-040C` (recipe wiring + reference-TAA resolve smoke).
