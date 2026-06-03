# GRAPHICS-040A — Camera jitter + motion-vector buffer

## Goal
- Land the Halton(2,3)×16 sub-pixel camera jitter as a projection-matrix override
  recorded into `CameraSnapshot::JitterOffset`, and the `R16G16_SFLOAT` screen-space
  motion-vector target produced by the surface/G-buffer pass (`GRAPHICS-040`
  decisions 1/2), with `contract;graphics` null-RHI tests.

## Non-goals
- No `IReconstructor` interface or reference TAA (that is `GRAPHICS-040B`).
- No recipe selection / post-chain integration (that is `GRAPHICS-040C`).
- No vendor backends (deferred `GRAPHICS-040` per-vendor children, not opened).

## Context
- Owner layers: `runtime` (camera jitter offset written into the snapshot) + `graphics/renderer`
  (motion-vector target production). The jitter offset is carried in the snapshot so it is
  the single authoritative value (decision 1) — no live ECS access from graphics.
- Depends on `GRAPHICS-040` (planning, done), `GRAPHICS-013A` (postprocess chain, done),
  and the `GRAPHICS-036` camera-snapshot path.
- Decision 1: Halton(2,3) length 16, advanced per rendered frame, injected as a projection
  override on `[2][0]`/`[2][1]` scaled by `2/renderWidth`, `2/renderHeight` (not a viewport
  offset); offset recorded into `CameraSnapshot::JitterOffset` (NDC).
- Decision 2: `R16G16_SFLOAT` motion-vector target storing current→previous NDC delta
  (jitter-removed); dynamic geometry from per-instance previous-frame MVP, static from
  camera-only reprojection.
- Decision 12: a `NoJitterNoHistory` recipe flag forces zero jitter for deterministic
  golden-image tests.

## Required changes
- [ ] Add the Halton(2,3)×16 jitter sequence + projection override; record `JitterOffset`.
- [ ] Add the `R16G16_SFLOAT` motion-vector frame-graph target produced by the surface/
      G-buffer pass (dynamic per-instance prev-MVP + static camera reprojection).
- [ ] Add the `NoJitterNoHistory` recipe flag forcing zero jitter.
- [ ] `contract;graphics` tests for jitter-sequence determinism (Halton replay), the
      projection override, and motion-vector target shape.

## Tests
- [ ] `contract;graphics` — Halton replay determinism; projection override math; MV target
      format/shape; `NoJitterNoHistory` forces zero jitter.
- [ ] CPU gate green.

## Docs
- [ ] Document jitter + motion vectors in `src/graphics/renderer/README.md` and the
      `JitterOffset` snapshot field in `src/runtime/README.md`.
- [ ] Regenerate the module inventory if surfaces change.

## Acceptance criteria
- [ ] Jitter is deterministic and snapshot-authoritative; the MV target is produced and CPU-tested.
- [ ] No new layering violations.
- [ ] `GRAPHICS-040B/C` remain the reconstructor/recipe follow-ups.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Injecting jitter as a viewport offset instead of a projection override.
- Direct ECS observation of the camera from graphics.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` under the null RHI for the jitter + motion-vector contract.
- `Operational` owned by `GRAPHICS-040C` (recipe selection + post-chain integration; the
  reference-TAA resolve `gpu;vulkan` smoke rides on that operational path).
