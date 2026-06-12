---
id: BUG-042
theme: F
depends_on: [BUG-028, BUG-032, GRAPHICS-071]
maturity_target: CPUContracted
---
# BUG-042 — Promoted impostor spheres do not intersect surfaces correctly

## Goal
- Restore promoted point-sphere impostor rendering parity with legacy behavior: sphere mode must render camera-facing sphere impostors with per-fragment sphere intersection and corrected depth so spheres intersect/occlude surfaces correctly.

## Non-goals
- No Gaussian splatting, EWA point-cloud overhaul, or anisotropic surfel work.
- No live ECS reads in graphics passes.
- No selection/picking point-ID behavior changes unless a compile contract requires matching shader topology.
- No broad renderer recipe reorder.

## Context
- Owner/layer: `graphics/renderer` owns forward point pass shader/pipeline contracts; `runtime` only publishes `GpuEntityConfig::PointMode` and `PointSize`.
- Legacy `PointPass` used `point_sphere.vert/frag`, expanded each point into a six-vertex billboard quad, ray-traced the sphere in the fragment shader, and wrote `gl_FragDepth`.
- Promoted `forward/point.vert/frag` uses hardware point sprites (`gl_PointSize`/`gl_PointCoord`) and sphere mode only shades a fake sphere; it never writes corrected depth.
- Ranked hypotheses from local diagnosis:
  1. Promoted point rendering lost the legacy billboard expansion, so sphere mode cannot compute true per-pixel sphere depth.
  2. The promoted fragment shader shades a sphere visually but depth remains the flat point primitive depth.
  3. Culling outputs one vertex per point, so the shader must expand that point to six vertices and the pipeline must use triangle-list topology for parity.
  4. Current `PointSize` is screen-space pixels, unlike the legacy world-radius path; this fix preserves promoted authoring semantics and computes view-space radius from viewport/projection.

## Required changes
- [x] Change the promoted forward point pipeline/shaders so point draw records expand one source point into a six-vertex billboard quad.
- [x] Make sphere mode compute the front sphere surface point and write corrected `gl_FragDepth`.
- [x] Preserve flat-circle and surfel modes under the same retained point path.
- [x] Update pipeline descriptor contracts from point-list to triangle-list where appropriate.
- [x] Remove any temporary diagnosis probes.

## Tests
- [x] Add/update graphics contract coverage pinning the promoted forward point pipeline topology and shader paths.
- [x] Add shader-source regression coverage for `gl_FragDepth` in sphere mode or compile the changed shader pair.
- [x] Keep existing point pass command contract passing against non-indexed cull bucket draws.

## Docs
- [x] Update `src/graphics/renderer/README.md` with the promoted point-sphere impostor depth contract.
- [x] Retire/update bug notes and regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] Promoted point sphere mode no longer relies on hardware point-sprite depth.
- [x] Sphere fragments outside the impostor footprint are discarded and surviving fragments write depth from the computed sphere surface.
- [x] Flat and surfel modes remain available from `GpuEntityConfig::PointMode`.
- [x] Focused graphics contract tests pass under the CPU gate.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle|LinePointPassContracts' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

2026-06-12 results:
- Commit: pending local generated texture/impostor commit.
- `cmake --preset ci` passed before focused and broad verification.
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicGraphicsContractTests IntrinsicAssetUnitTests` passed.
- Focused generated texture/impostor/material CTest set passed 43/43.
- `cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests` passed.
- Focused point-impostor regression set passed 3/3, including `RendererFrameLifecycle.ForwardPointSphereImpostorsWriteCorrectedDepth`.
- `cmake --build --preset ci --target IntrinsicTests` passed.
- Default CPU-supported CTest gate passed 3008/3008.
- Direct `glslc` compile of edited shader sources passed.
- Structural checks passed: task policy, doc links, layering, test layout, docs-sync diff mode, PR contract, and `git diff --check`. Root hygiene remains warning-mode with existing `imgui.ini`.

## Forbidden changes
- Copying legacy pass code into promoted graphics instead of reimplementing against `GpuWorld`/cull-bucket contracts.
- Adding ECS/runtime imports to `src/graphics/*`.
- Changing point authoring units without a separate runtime/editor task.

## Maturity
- Closed at `CPUContracted`; a future opt-in `gpu;vulkan` image/depth readback smoke may upgrade this to `Operational`.
