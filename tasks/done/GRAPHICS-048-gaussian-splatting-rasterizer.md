# GRAPHICS-048 — 3D Gaussian Splatting rasterizer pass over the PointCloud primitive (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until Theme A `PointCloud` residency consumers need the extended record.

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

## Recorded decisions
1. **Extended point record.** Per Gaussian: position (vec3), scale (vec3), rotation (quat), opacity (float), SH coefficients (45 floats for SH3 RGB, or 3 floats for the SH0 baseline), plus a per-cloud SH-band capability flag selecting the stored coefficient count. Rationale: storing scale+quat rather than a baked 3×3 covariance halves the per-Gaussian footprint and keeps the covariance reconstructable on the GPU, and a per-cloud SH-band flag lets low-order clouds avoid paying for 45 coefficients they do not use.
2. **Asset shape.** `.ply` and `.splat` ingest in `assets/` produces a binary `.gsplat` shipping format with a recorded version, per-Gaussian record layout, and little-endian byte order. Rationale: a single canonical engine-owned shipping format decouples the renderer from the messy variety of community `.ply`/`.splat` layouts, and pinning version + byte order makes the format forward-compatible and portable.
3. **GPU residency.** 3DGS clouds reuse the `PointCloud` GPU-residency path; the per-point payload is wider but the lifecycle is identical. Rationale: reusing the existing point-cloud residency avoids a parallel cache/upload path — only the per-point stride changes — so 3DGS inherits refcount/retire semantics for free.
4. **Tile-based rasterizer.** A 16×16 tile grid with a per-tile Gaussian list built by a culling pass that projects each Gaussian's screen-space 2D extent; each Gaussian is bucketed into every tile its projected extent touches. Rationale: 16² tiling is the canonical 3DGS work decomposition — it bounds per-pixel overdraw to a tile's local list and makes the depth sort tile-local rather than global, which is the key to real-time splat compositing.
5. **Sort.** Per-tile depth sort via a parallel radix (or bitonic) sort over the per-tile list, with a recorded max-Gaussians-per-tile cap and an overflow diagnostic when exceeded. Rationale: correct alpha compositing requires depth order; a tile-local parallel sort is cheap, and a hard per-tile cap with an overflow counter bounds worst-case memory while surfacing pathological tiles instead of silently corrupting them.
6. **Alpha compositing.** Front-to-back compositing with early termination once accumulated opacity saturates, at a recorded early-out threshold (default transmittance < 1/255). Rationale: front-to-back with a saturation early-out is the standard splat composite — it skips occluded back Gaussians, which is the dominant cost saver in dense clouds, at a threshold below visible precision.
7. **SH color evaluation.** Each Gaussian evaluates its SH color in the view direction, with the SH band count a per-cloud capability flag (`SH0`, `SH1`, `SH2`, `SH3`). Rationale: view-dependent SH color is what gives 3DGS its specular/anisotropic appearance; making the band count a per-cloud flag lets the same kernel serve baseline `SH0` clouds and full `SH3` clouds without recompilation per cloud.
8. **Recipe integration.** `Pass.Splat` runs after `Pass.Forward.Surface` and before post-processing, reads `SceneDepth` for occlusion against meshes, and writes `SceneColorHDR`. Rationale: ordering after the surface pass lets splats depth-test against opaque mesh geometry (correct mesh/splat interleaving) while writing into the shared HDR target keeps splats a composable contribution that post-processing treats uniformly.
9. **Lane integration.** 3DGS does not consume the existing 8-bucket lane contract; it is a separate primitive lane and the lanes are not renumbered. Rationale: the 8-bucket contract is depended on by surface/line/point/shadow/selection passes, so adding splats as a distinct lane (rather than overloading a bucket) preserves that contract and keeps splat work independently schedulable.
10. **Diagnostics.** `SplatTileOverflowCount`, `SplatGaussiansRenderedCount`, and `SplatEarlyOutFraction` are atomic counters. Rationale: overflow surfaces capped tiles, the rendered count surfaces post-cull workload, and the early-out fraction surfaces composite efficiency — the three signals needed to tune tile cap and threshold, all without strings.
11. **Test split.** `unit` for SH evaluation correctness; `contract;graphics` for tile assignment, sort wiring, and recipe integration under null RHI; opt-in `gpu;vulkan` smoke for golden-image splat correctness on a known scene. Rationale: SH math is a pure CPU-checkable function, tile/sort/recipe wiring is device-independent, and only the composited image needs a device — keeping the default gate green.
12. **Layering.** No live ECS; `assets/` owns `.ply` / `.splat` ingest, and graphics consumes `.gsplat` only. Rationale: preserves AGENTS.md §2 — CPU geometry/format parsing lives in `assets`, graphics consumes the baked shipping format and never imports a file parser.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-048-Impl-A** — Extended point record + `.gsplat` asset format + `assets/` ingest + `unit` tests.
- **GRAPHICS-048-Impl-B** — GPU residency through `Graphics.GpuAssetCache` + integration tests.
- **GRAPHICS-048-Impl-C** — Tile assignment + sort + null-RHI shape tests.
- **GRAPHICS-048-Impl-D** — Rasterizer + SH evaluation + composite + integration tests.
- **GRAPHICS-048-Impl-E** — Opt-in `gpu;vulkan` golden-image test on a small known scene.

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] Recording 3DGS as a peer primitive in `docs/architecture/rendering-three-pass.md` is deferred to the implementation children (`GRAPHICS-048-Impl-C/D`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The primitive section of `src/graphics/renderer/README.md` is deferred to the same implementation children for the same reason.
- [x] The primitive-ownership section of `docs/architecture/graphics.md` is deferred to the same implementation children for the same reason.

## Acceptance criteria
- [x] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] 8-bucket lane contract preserved.
- [x] Existing `PointCloud` primitive lifecycle unchanged for non-Gaussian clouds.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All twelve 3DGS decisions are recorded with explicit answers and trade-off rationales: the scale+quat+opacity+SH extended point record with a per-cloud band flag, the engine-owned versioned little-endian `.gsplat` shipping format, `PointCloud`-residency reuse, the 16² tile grid with screen-extent bucketing, the per-tile parallel sort with a capped overflow diagnostic, front-to-back compositing with a transmittance early-out, view-direction SH evaluation gated by a per-cloud band flag, the `Pass.Splat` post-surface / pre-post-process ordering with `SceneDepth` occlusion into `SceneColorHDR`, the separate non-renumbered splat lane, the three atomic splat counters, the unit-SH / null-RHI-contract / opt-in-`gpu;vulkan` test split, and the `assets`-owns-ingest layering audit. Implementation children `GRAPHICS-048-Impl-A..E` are identified but not opened; the 8-bucket lane contract and the non-Gaussian `PointCloud` lifecycle stay unchanged and no training pipeline lands. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No 3DGS training pipeline in this slice.
- No removal of mesh / point / line primitives.
- No 8-bucket renumbering.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
