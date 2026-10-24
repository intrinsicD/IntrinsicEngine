---
id: BUG-248
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: bug filed from GPU-lane diagnosis runs of the 2026-10-24 night; no implementation in this slice
contract_schema: 1
contracts: [geometry.property-coherence]
---
# BUG-248 — Keypoint GPU parity smokes fail on main

## Goal
- Restore CPU/GPU keypoint parity in the two failing `ci-vulkan` smokes, or record a reviewed
  tolerance decision, without weakening or removing their assertions.

## Context
- Observed on `ci-vulkan` at `599f02ff1` (the tip of `main`) plus the merged BUG-182 test fix
  (`2df2a2837`, which changes only the framed kNN case and one import), on an NVIDIA GeForce
  RTX 4090 with driver 580.159.04, 2026-10-24 01:57–02:02 CEST.
- The failures do not depend on the display: the same failure lines on `DISPLAY=:1` and on an
  owned Xephyr `:9` (`cohort-d1.log`, `cohort-d9.log`; they differ only in timings). In each
  run 19 of 21 selected cases passed.
- Line numbers below are at `2df2a2837`. With RUNTIME-291's three-line test addition merged,
  the resident lines are 3 higher (`:2833`, `:2836`, `:2852`, `:2858`).
- `PointLBVHGpuSmoke.KeypointVulkanComputePlanarZeroScoresAndIsotropicFloor` fails at
  `Test.PointLBVHGpuSmoke.cpp:1981` 8 times (`KeypointCount` 5, reference 8, in each of the
  eight domains) and at `:1989` 8 times (`keypoints` mask differs from the reference).
- `PointLBVHGpuSmoke.KeypointResidentSubnormalScores` fails 12 times at `:2830` (preview mask)
  and 12 times at `:2849` (mask after accept). The saliency L∞ bound fails at `:2833` and
  `:2855`: 7.58e-42 against 1.12e-44 (8 × `denorm_min`).
- RUNTIME-291's note ([RUNTIME-291](../runtime/RUNTIME-291-scale-aware-radius-defaults.md))
  records both cases failing the same way with its `src/` change reverted, so they predate it.
- CI coverage: the cases are `gpu;vulkan`-labelled, so no CPU gate selects them. The hosted
  `ci-vulkan` workflow (pull requests and merge queue only, lavapipe, displayless batch)
  includes `IntrinsicPointLBVHGpuTests` in its aggregate; whether these cases skip there was
  not checked, and its last run was on 2026-09-23. The self-hosted nightly GPU job is opt-in.
- Raw logs (git-ignored): `tasks/evidence/BUG-248/cohort-d1.log` and `cohort-d9.log`, copied
  byte-for-byte from the GPU lane's `tasks/evidence/BUG-228/2026-10-24/`.

## Acceptance criteria
- [ ] Reproduce both failures on `ci-vulkan` at the current integration head and name the root
      cause (GPU kernel, CPU reference or test expectation) with evidence.
- [ ] Both cases pass on a Vulkan host with every existing assertion kept; any tolerance change
      is recorded with its reasoning and approved in review before it lands.
- [ ] The other keypoint cases in the same selection still pass.

## Verification
```bash
nice -n 10 cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.KeypointVulkanComputePlanarZeroScoresAndIsotropicFloor$' -L gpu -L vulkan --timeout 120
ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke.KeypointResidentSubnormalScores$' -L gpu -L vulkan --timeout 120
ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke\.Keypoint' -L gpu -L vulkan --timeout 120
```
