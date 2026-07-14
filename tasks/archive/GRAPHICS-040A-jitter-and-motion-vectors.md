# GRAPHICS-040A — Camera jitter + motion-vector buffer

## Status
- Commit reference: this task-landing commit.
- Status: `completed`
- Owner/agent: Codex
- Branch: `main`
- Started: 2026-06-05
- Completed: 2026-06-05
- Current slice: single-slice `CPUContracted` jitter + motion-vector contract.
  The slice keeps jitter snapshot-authoritative, adds the backend-neutral
  motion-vector target shape to the frame recipe, and leaves reconstruction and
  post-chain integration to `GRAPHICS-040B/C`.
- Next verification step: retired. Reference TAA reconstruction remains
  `GRAPHICS-040B`; recipe selection and post-chain integration remain
  `GRAPHICS-040C`.

## Goal
- Land the Halton(2,3)×16 sub-pixel camera jitter as a projection-matrix override
  recorded into `TemporalCameraViewSnapshot::JitterOffset`, and the `R16G16_SFLOAT` screen-space
  motion-vector target declared by the surface/G-buffer pass (`GRAPHICS-040`
  decisions 1/2), with `contract;graphics` null-RHI tests.

## Non-goals
- No `IReconstructor` interface or reference TAA (that is `GRAPHICS-040B`).
- No recipe selection / post-chain integration (that is `GRAPHICS-040C`).
- No vendor backends (deferred `GRAPHICS-040` per-vendor children, not opened).

## Context
- Owner layer: `graphics/renderer` (camera jitter helper + motion-vector target contract).
  The jitter offset is carried by the temporal camera snapshot wrapper so it is the
  single authoritative value (decision 1) — no live ECS access from graphics.
- Depends on `GRAPHICS-040` (planning, done), `GRAPHICS-013A` (postprocess chain, done),
  and the `GRAPHICS-036` camera-snapshot path.
- Decision 1: Halton(2,3) length 16, advanced per rendered frame, injected as a projection
  override on `[2][0]`/`[2][1]` scaled by `2/renderWidth`, `2/renderHeight` (not a viewport
  offset); offset recorded into `TemporalCameraViewSnapshot::JitterOffset` (NDC).
- Decision 2: `R16G16_SFLOAT` motion-vector target storing current→previous NDC delta
  (jitter-removed); dynamic geometry from per-instance previous-frame MVP, static from
  camera-only reprojection.
- Decision 12: `FrameRecipeTemporalOptions::NoJitterNoHistory` suppresses
  motion/history frame-recipe surfaces, and the matching camera helper flag
  forces zero jitter for deterministic golden-image tests.

## Required changes
- [x] Add the Halton(2,3)×16 jitter sequence + projection override; record `JitterOffset`.
- [x] Add the `R16G16_SFLOAT` motion-vector frame-graph target declared by the surface/
      G-buffer pass; shader encoding remains `GRAPHICS-040B/C`.
- [x] Add the `NoJitterNoHistory` temporal recipe/camera option for deterministic no-history frames.
- [x] `contract;graphics` tests for jitter-sequence determinism (Halton replay), the
      projection override, and motion-vector target shape.

## Tests
- [x] `contract;graphics` — Halton replay determinism; projection override math; MV target
      format/shape; `NoJitterNoHistory` forces zero jitter.
- [x] CPU gate green.

## Docs
- [x] Document jitter + motion vectors in `src/graphics/renderer/README.md` and the
      temporal camera helper contract in `src/runtime/README.md`.
- [x] Regenerate the module inventory if surfaces change.

## Acceptance criteria
- [x] Jitter is deterministic and snapshot-authoritative; the MV target is declared/attached and CPU-tested.
- [x] No new layering violations.
- [x] `GRAPHICS-040B/C` remain the reconstructor/recipe follow-ups.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RenderWorldContract\.(TemporalJitterHaltonSequenceIsDeterministicAndWraps|TemporalJitterAppliesProjectionOverrideAndSnapshotOffset|NoJitterNoHistoryForcesZeroCameraJitter)|FrameRecipeContract\.(MotionVectorTargetIsOptInRg16SurfaceOutput|NoJitterNoHistorySuppressesMotionVectorTarget|OptionalResourcesAreGatedByFeatures)' --timeout 60
ctest --test-dir build/ci --output-on-failure -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'IntrinsicBenchmarkSmoke' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
git diff --check
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
```

## Forbidden changes
- Injecting jitter as a viewport offset instead of a projection override.
- Direct ECS observation of the camera from graphics.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Achieved: `CPUContracted` under the null RHI for the jitter + motion-vector contract.
- `Operational` owned by `GRAPHICS-040C` (recipe selection + post-chain integration; the
  reference-TAA resolve `gpu;vulkan` smoke rides on that operational path).
