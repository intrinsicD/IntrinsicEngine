# GRAPHICS-038B — HZB build compute shader + dispatch wiring

## Goal
- Implement the HZB build compute pass (`hzb_build.comp`, max-reduction down-sampler)
  with the single-pass SPD-style mip-chain path where supported and the per-mip dispatch
  fallback otherwise (`GRAPHICS-038` decision 2), with null-RHI dispatch-shape tests.

## Non-goals
- No cull-shader phase-1/phase-2 extension (that is `GRAPHICS-038C`).
- No camera-transition heuristic (that is `GRAPHICS-038D`).

## Context
- Owner layer: `graphics/renderer` (build pass) producing into the `GRAPHICS-038A` HZB.
- Depends on `GRAPHICS-038A` (HZB resource + lifetime).
- Decision 2: compute shader, one workgroup per output tile using `subgroupMax` where
  available else shared-memory tree max-reduction; single-pass mip-chain (SPD-style,
  last-workgroup mip stitching via global atomics) where available, per-mip dispatch
  fallback otherwise.

## Required changes
- [ ] Add `assets/shaders/.../hzb_build.comp` with subgroup + shared-memory reduction paths.
- [ ] Wire the build pass (single-pass mip-chain + per-mip fallback) into the recipe.
- [ ] `contract;graphics` null-RHI tests for dispatch shape, mip-stitch ordering, and
      the fallback path selection.

## Tests
- [ ] `contract;graphics` — dispatch count per path; correct per-mip coverage; fallback
      selection when single-pass capability is absent.
- [ ] CPU gate green.

## Docs
- [ ] Document the HZB build pass in `src/graphics/renderer/README.md`.

## Acceptance criteria
- [ ] The HZB build pass is wired and CPU-tested for dispatch shape (both paths).
- [ ] No new layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -L graphics -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Live ECS access from renderer code.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` under the null RHI for the build-pass dispatch contract.
- `Operational` owned by `GRAPHICS-038E` (opt-in `gpu;vulkan` conservatism smoke).
