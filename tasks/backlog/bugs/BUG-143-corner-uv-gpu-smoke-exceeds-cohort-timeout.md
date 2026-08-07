---
id: BUG-143
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - geometry.property-coherence
---
# BUG-143 — Corner-UV `gpu;vulkan` smoke exceeds the 30 s cohort timeout

## Goal
- Land the `BUG-137` slice B `Operational` readback smoke so that a seam-split
  corner-UV mesh is proven to render on a real Vulkan backend, inside the
  30 s per-test budget the `gpu;vulkan` cohort actually enforces.

## Non-goals
- No weakening or deletion of the readback assertions to fit the budget.
- No `slow` label on `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests`; that
  label is applied per executable and would drop the whole cohort out of the
  required gate.
- No change to the corner-UV upload behaviour itself, which is already covered
  on CPU by `Test.CornerTexcoordUpload` and `Test.MeshGeometryExtraction`.

## Context
- The smoke was authored, run green on hardware, and committed as `e1416f08`,
  then reverted from `sandbox-workflow-audit-fixes` on 2026-08-07 because it
  times out in the gate.
- Evidence, NVIDIA GeForce RTX 4090, driver 580.159.04, `ci-vulkan` preset
  (combined ASan+UBSan):
    - First standalone runs: 12.7 s, 12.9 s — comfortably green.
    - Full `-L gpu -L vulkan` gate: `***Timeout 30.14 s`, reproduced on a
      second, uncontended gate run (`***Timeout 30.13 s`).
    - A later standalone run of the identical binary and filter: **33.9 s**.
- So this is not a ctest-environment artifact: the test's own wall clock varies
  between roughly 13 s and 34 s, straddling the 30 s limit that
  `tests/CMakeLists.txt` applies to every non-`slow` executable
  (`_intrinsic_default_test_timeout_seconds`).
- Reducing the frame budget does **not** help and measured *worse* (16 frames
  → 18 s versus 96 frames → 13 s): cutting the loop short moves the async
  enrichment and generated-normal bake into shutdown, which then blocks.
  Composing without `TextureBakeModule` (32 frames) still timed out.
- The variance source is therefore not yet identified. Suspects, in the order
  worth ruling out: swapchain/present pacing in `ExitAfterFramesApp` under a
  non-VSync config; the deferred direct-mesh enrichment job's wake latency; and
  first-use pipeline/shader compilation attributed to whichever test runs first.
- Note `RuntimeSandboxAcceptanceGpuSmoke.PropertyTextureModuleBakesRebindsRebakesAndRemovesOnVulkan`
  already runs at 25.5 s in the same cohort, so the headroom problem is not
  unique to this test and a shared diagnosis may cover both.

## Required changes
- [ ] Diagnose the 13 s → 34 s variance rather than padding the budget around
      it (`intrinsicengine-diagnose`: deterministic loop, ranked hypotheses,
      tagged probes).
- [ ] Restore the smoke from `e1416f08` — the test body and the
      `ReadSurfaceGeometryRecordByEntityId` helper are both in that commit.
- [ ] Keep its assertions intact: ECS mesh 8 V / 18 E / 36 H / 12 F carrying
      `h:texcoord` and no `v:texcoord`, GPU geometry record reporting 24
      vertices and 36 surface indices, and a center pixel distinguishable from
      three background corners.

## Tests
- [ ] The restored smoke passes under
      `ctest --test-dir build/ci-vulkan -L gpu -L vulkan` with the cohort's
      default timeout, on three consecutive runs.
- [ ] Default CPU gate stays green.

## Docs
- [ ] If the fix changes the smoke authoring pattern (frame budgeting, bootstrap
      choice), record it in `docs/agent/` and re-sync
      `intrinsicengine-gpu-smoke-authoring`.

## Acceptance criteria
- [ ] `BUG-137` slice B closes `Operational` with a cited, in-budget gate run.
- [ ] No readback assertion was weakened to get there.
- [ ] The `gpu;vulkan` gate has no new timeout or flake.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R 'SeamSplitCornerUv' -L 'gpu' -L 'vulkan'
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan'
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Relaxing, skipping, or deleting a readback assertion to reach green.
- Labelling the smoke `flaky-quarantine` without a recorded diagnosis.
- Raising `_intrinsic_default_test_timeout_seconds` for the whole tree to
  accommodate one test.

## Maturity
- Target: `Operational` on Vulkan-capable hosts. The corner-UV upload path is
  already `CPUContracted` and stays that way while this is open.
