# GRAPHICS-038E — Opt-in gpu;vulkan HZB conservatism smoke

## Goal
- Add an opt-in `gpu;vulkan` smoke validating HZB occlusion conservatism on a
  Vulkan-capable host (`GRAPHICS-038` decision 12): a known-visible probe instance is
  never over-rejected by the two-phase cull.

## Non-goals
- No change to the CPU/null contracts from `GRAPHICS-038A/B/C/D`.

## Context
- Owner layer: `graphics` integration test under `gpu;vulkan;graphics` labels (excluded
  from the default CPU gate).
- Depends on `GRAPHICS-038A/B/C/D` and `GRAPHICS-033` (operational Vulkan gate, done).
- Decision 12: the smoke validates HZB conservatism — no over-rejection of a known-
  visible probe instance — running only on hosts with Vulkan + GLFW.

## Required changes
- [ ] Add `tests/integration/graphics/Test.HzbOcclusionConservatismGpuSmoke.cpp` under
      `gpu;vulkan;graphics` labels.
- [ ] Drive a real two-phase cull frame and assert the probe instance survives (no
      over-rejection) and the disocclusion path rescues a known-disoccluded instance.

## Tests
- [ ] `gpu;vulkan` smoke — probe conservatism + disocclusion rescue on a Vulkan host;
      skips when no operational Vulkan/GLFW lane is present.
- [ ] Default CPU gate unaffected.

## Docs
- [ ] Note the smoke fixture in `src/graphics/renderer/README.md`.

## Acceptance criteria
- [ ] The conservatism smoke passes on a Vulkan-capable host and skips elsewhere.
- [ ] No new layering violations; CPU gate unchanged.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan -L 'gpu' -L 'vulkan' --output-on-failure
```

## Forbidden changes
- Changing the CPU/null contracts from `GRAPHICS-038A/B/C/D`.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `Operational` on Vulkan-capable hosts via the opt-in smoke; the CPU two-phase
  cull contract remains the default-gate authority elsewhere.
