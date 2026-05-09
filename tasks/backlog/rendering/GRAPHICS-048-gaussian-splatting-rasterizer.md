# GRAPHICS-048 — 3D Gaussian Splatting rasterizer pass over the PointCloud primitive (planning)

## Goal
Lock down the contract for a 3D Gaussian Splatting (3DGS) rasterizer pass that consumes the existing `PointCloud` primitive (extended with per-point Gaussian parameters: anisotropic covariance, opacity, spherical-harmonic color coefficients) and rasterizes radiance fields as a peer rendering path to mesh-based surfaces. Planning only — no shader bodies and no SH evaluation kernels here.

## Non-goals
- No NeRF or other implicit-field renderers.
- No 3DGS *training* / fitting pipeline (that lives under `methods/` if and when it lands).
- No replacement of mesh / point / line primitives; 3DGS is a peer.
- No global illumination integration; 3DGS samples scene radiance directly.
- No streaming / GPU-resident-scene integration here (`GRAPHICS-052`/`GRAPHICS-055` cover those).

## Context
- Owner layer: `graphics/renderer` (3DGS pass + tile-based sort), `graphics/rhi` (existing storage-buffer surface), `graphics/assets` (3DGS asset upload), `assets` (CPU-side .ply / .splat ingest).
- 3D Gaussian Splatting (Kerbl et al., SIGGRAPH 2023 best paper) is the dominant rasterizable radiance-field representation. Follow-ups (3DGS-DR, RadSplat, foveated VR-Splatting, 2DGS) extend the primitive but share the core rasterizer.
- IntrinsicEngine's `PointCloud` primitive is already a peer to mesh / graph at the GPU residency level. This task formalizes the extension to per-point Gaussian parameters and the rasterizer that consumes them.
- Cross-links: `GRAPHICS-014` (visualization attributes; SH-color overlay shares routes), `GRAPHICS-030` (procedural-source geometry residency bridge; 3DGS clouds are a procedural source), `GRAPHICS-038` (HZB cull at point granularity), `GRAPHICS-039` (cluster light list does not apply; 3DGS carries baked radiance).

## Design decisions to record
1. **Extended point record.** Per Gaussian: position (vec3), scale (vec3), rotation (quat), opacity (float), SH coefficients (45 floats for SH3 RGB; or 3 floats for SH0 baseline). Record the canonical layout and the SH-band capability flag.
2. **Asset shape.** `.ply` and `.splat` ingest in `assets/` produces a binary `.gsplat` shipping format. Record the shipping format spec (version, per-Gaussian record layout, byte order).
3. **GPU residency.** 3DGS clouds reuse the `PointCloud` GPU-residency path; the per-point payload is wider but the lifecycle is identical.
4. **Tile-based rasterizer.** 16×16 tile grid; per-tile Gaussian list built by a culling pass that projects each Gaussian's screen-space 2D extent. Record the tile assignment rule and the per-tile depth-sort policy.
5. **Sort.** Per-tile depth sort using a parallel radix or bitonic sort over the per-tile list. Record the sort algorithm + max-Gaussians-per-tile cap with overflow diagnostic.
6. **Alpha compositing.** Front-to-back compositing with early termination on saturation. Record the early-out threshold.
7. **SH color evaluation.** Each Gaussian evaluates its SH color in the view direction. SH band count is a per-cloud capability flag (`SH0`, `SH1`, `SH2`, `SH3`). Record the rule.
8. **Recipe integration.** `Pass.Splat` runs after `Pass.Forward.Surface` and before post-processing; reads `SceneDepth` for occlusion against meshes; writes `SceneColorHDR`. Record the order rule.
9. **Lane integration.** 3DGS does not consume the existing 8-bucket lane contract; it's a separate primitive lane. Record the rule that lanes are not renumbered.
10. **Diagnostics.** `SplatTileOverflowCount`, `SplatGaussiansRenderedCount`, `SplatEarlyOutFraction`. Atomic counters.
11. **Test split.** `unit` for SH evaluation correctness; `contract;graphics` for tile assignment, sort wiring, recipe integration under null RHI; opt-in `gpu;vulkan` smoke for golden-image splat correctness on a known scene.
12. **Layering.** No live ECS. `assets/` owns `.ply` / `.splat` ingest; graphics consumes `.gsplat` only.

## Required changes
- Capture the design decisions above as explicit recorded answers with trade-off rationales.
- Cross-link upstream and downstream tasks enumerated in Context.
- Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-048-Impl-A** — Extended point record + `.gsplat` asset format + `assets/` ingest + `unit` tests.
- **GRAPHICS-048-Impl-B** — GPU residency through `Graphics.GpuAssetCache` + integration tests.
- **GRAPHICS-048-Impl-C** — Tile assignment + sort + null-RHI shape tests.
- **GRAPHICS-048-Impl-D** — Rasterizer + SH evaluation + composite + integration tests.
- **GRAPHICS-048-Impl-E** — Opt-in `gpu;vulkan` golden-image test on a small known scene.

## Tests
- Planning slice: validators only.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/rendering-three-pass.md` to record 3DGS as a peer primitive.
- Update `src/graphics/renderer/README.md` primitive section.
- Update `docs/architecture/graphics.md` primitive ownership.

## Acceptance criteria
- Twelve decisions are recorded with explicit answers and trade-off rationales.
- Implementation child slices are identified but not opened.
- 8-bucket lane contract preserved.
- Existing `PointCloud` primitive lifecycle unchanged for non-Gaussian clouds.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No 3DGS training pipeline in this slice.
- No removal of mesh / point / line primitives.
- No 8-bucket renumbering.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
