# GRAPHICS-038A — HZB resource shape, frame-graph lifetime, ping-pong policy

## Goal
- Land the Hi-Z buffer resource (`R32_SFLOAT`, pow2-sized, full mip chain storing
  conservative max-depth) and its retained graphics-owned cross-frame ping-pong
  lifetime via the `GRAPHICS-015Q` retire-deadline pattern (`GRAPHICS-038` decisions 1/3),
  with `contract;graphics` tests for shape, mip count, and lifetime.

## Non-goals
- No HZB build compute shader (that is `GRAPHICS-038B`).
- No cull-shader phase-1/phase-2 extension (that is `GRAPHICS-038C`).
- No camera-transition heuristic / selection exemption (that is `GRAPHICS-038D`).

## Context
- Owner layer: `graphics/framegraph` (resource lifetime) + `graphics/renderer` (HZB ownership).
- Depends on `GRAPHICS-038` (planning, done) and `GRAPHICS-007` (culling buckets, done).
- Decision 1: `R32_SFLOAT`, sized to the next pow2 of the render extent, halved per mip
  to 1×1, mip count `floor(log2(max(w,h))) + 1`, storing per-tile conservative **max**
  depth (built with a max reduction). The no-false-rejection invariant: an instance is
  culled only when its screen-bounded nearest depth exceeds the sampled HZB max-depth.
- Decision 3: a retained graphics-owned HZB carried across frames, ping-ponged so phase 1
  reads the previous-frame HZB and phase 2 writes the current-frame HZB; resize/format
  changes reallocate through the retire-deadline window.

## Status
- Commit reference: this task-landing commit.
- Landed 2026-06-04 at maturity `CPUContracted`. New renderer-owned module
  `Extrinsic.Graphics.HZB` (`src/graphics/renderer/Graphics.HZB.{cppm,cpp}`,
  mirroring the `ShadowSystem` retained-resource ownership): `HZBDesc` +
  pure `NextPow2`/`ComputeHZBDesc` sizing (pow2 each dim, full mip chain to 1x1,
  `RHI::Format::R32_FLOAT`), and `HZBSystem` owning the two-texture ping-pong
  pair with per-frame role swap (`AdvanceFrame`), reallocation on extent/format
  change, and a `framesInFlight` retire-deadline window (`Tick`). Verified:
  `IntrinsicGraphicsContractCpuTests` 226/226 (4 new
  `GraphicsHZBResourceLifetime` cases); layering/test-layout clean; module
  inventory regenerated (+1 module). `GRAPHICS-038B/C/D/E` own the build
  shader / cull extension / heuristic / `gpu;vulkan` smoke. The Vulkan
  `R32_SFLOAT` is the engine's `RHI::Format::R32_FLOAT`.

## Required changes
- [x] Declare the HZB resource descriptor (`R32_SFLOAT`, pow2 extent, full mip chain).
- [x] Add the retained ping-pong pair owned by the renderer with `framesInFlight` retire
      deadlines (mirroring `GRAPHICS-015Q`); reallocate on extent/format change.
- [x] `contract;graphics` tests for resource shape, mip-count formula, and ping-pong/
      retire-deadline lifetime under the null RHI.

## Tests
- [x] `contract;graphics` — pow2 sizing + mip-count formula; ping-pong identity across
      frames; reallocation on resize through the retire window; no leak.
- [x] CPU gate green.

## Docs
- [x] Document the HZB resource + lifetime in `src/graphics/renderer/README.md`.
- [x] Regenerate the module inventory if surfaces change.

## Acceptance criteria
- [x] The HZB resource shape and retained ping-pong lifetime exist and are CPU-tested.
- [x] No new layering violations.
- [x] `GRAPHICS-038B/C/D/E` remain the build/cull/heuristic/smoke follow-ups.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Using a format (e.g. `R16_UNORM`) that can quantize the conservative bound the wrong way.
- Live ECS access from frame-graph/renderer code.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` under the null RHI for the HZB resource + lifetime contract.
- `Operational` owned by `GRAPHICS-038E` (opt-in `gpu;vulkan` conservatism smoke).
