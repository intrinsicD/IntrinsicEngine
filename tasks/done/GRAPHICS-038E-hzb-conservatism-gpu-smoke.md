# GRAPHICS-038E — Opt-in gpu;vulkan HZB conservatism smoke

## Status
- Commit reference: this task-landing commit.
- Status: `completed`
- Owner/agent: Codex
- Branch: `main`
- Started: 2026-06-04
- Completed: 2026-06-04
- Current slice: single-slice `Operational` implementation on Vulkan-capable
  hosts. Added an opt-in `gpu;vulkan;graphics` smoke that runs a real Vulkan
  compute dispatch for the two-phase HZB cull predicate, compares GPU decisions
  against the CPU `ComputeTwoPhaseCullPartition(...)` contract, proves a known-
  visible probe is not over-rejected, proves a disoccluded instance is rescued,
  and preserves the default CPU/null contracts from `GRAPHICS-038A/B/C/D`.
- Next verification step: retired. Production HZB storage-image descriptor
  publication and full culling-shader sampling remain future backend descriptor
  integration work; this slice closes the opt-in conservatism proof.

## Goal
- Add an opt-in `gpu;vulkan` smoke validating HZB occlusion conservatism on a
  Vulkan-capable host (`GRAPHICS-038` decision 12): a known-visible probe instance is
  never over-rejected by the two-phase cull.

## Non-goals
- No change to the CPU/null contracts from `GRAPHICS-038A/B/C/D`.
- No speculative production HZB descriptor plumbing beyond the existing HZB build
  and culling contracts.

## Context
- Owner layer: `graphics` integration test under `gpu;vulkan;graphics` labels (excluded
  from the default CPU gate).
- Depends on `GRAPHICS-038A/B/C/D` and `GRAPHICS-033` (operational Vulkan gate, done).
- Decision 12: the smoke validates HZB conservatism — no over-rejection of a known-
  visible probe instance — running only on hosts with Vulkan + GLFW.

## Required changes
- [x] Add `tests/integration/graphics/Test.HzbOcclusionConservatismGpuSmoke.cpp` under
      `gpu;vulkan;graphics` labels.
- [x] Add `assets/shaders/tests/hzb_conservatism_smoke.comp`, a test-only compute
      shader that evaluates explicit HZB samples and writes phase/counter results.
- [x] Drive a real Vulkan compute dispatch and assert the probe instance survives
      (no over-rejection) and the disocclusion path rescues a known-disoccluded
      instance.
- [x] Split the stale renderer debug-dump integration assertion discovered by the
      broad `gpu;vulkan` lane into forward and deferred expectations.

## Tests
- [x] `gpu;vulkan` smoke — probe conservatism + disocclusion rescue on a Vulkan host;
      skips when no operational Vulkan/GLFW lane is present.
- [x] GPU decisions and counters are compared with the authoritative CPU
      `ComputeTwoPhaseCullPartition(...)` contract.
- [x] Default CPU gate unaffected.

## Docs
- [x] Note the smoke fixture and current production descriptor boundary in
      `src/graphics/renderer/README.md`.
- [x] Mirror the HZB build/cull boundary in `docs/architecture/rendering-three-pass.md`
      and `assets/shaders/hzb_build.comp` comments.

## Acceptance criteria
- [x] The conservatism smoke passes on a Vulkan-capable host and skips elsewhere.
- [x] No-over-rejection, disocclusion rescue, persistent rejection, invalid-previous-
      sample conservatism, frustum-first rejection, and selection-bucket exemption
      cases are covered.
- [x] No new layering violations; CPU gate unchanged.

## Verification
```bash
glslc assets/shaders/tests/hzb_conservatism_smoke.comp -I assets/shaders -o /tmp/hzb_conservatism_smoke.comp.spv --target-env=vulkan1.3
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'HzbOcclusionConservatismGpuSmoke' --timeout 120
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
cmake --build --preset ci-vulkan --target IntrinsicTests
cmake --build --preset ci-vulkan --target IntrinsicRuntimeIntegrationTests
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'GraphicsRenderer.NullRenderer(Forward|Deferred)DebugDump' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'GraphicsRenderer.NullRenderer(Forward|Deferred)DebugDump' --timeout 60
python3 tools/repo/check_shader_outputs.py --dir build/ci-vulkan/bin/shaders --require tests/hzb_conservatism_smoke.comp.spv
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Changing the CPU/null contracts from `GRAPHICS-038A/B/C/D`.
- Mixing mechanical file moves with semantic refactors.
- Routing production renderer APIs through Vulkan-specific HZB types.

## Maturity
- Achieved: `Operational` on Vulkan-capable hosts via the opt-in smoke; the CPU
  two-phase cull contract remains the default-gate authority elsewhere.
