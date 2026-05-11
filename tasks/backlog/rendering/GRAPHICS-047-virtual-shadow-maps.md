# GRAPHICS-047 — Virtual Shadow Maps to replace cascade atlas (planning)

## Goal
Lock down the contract for replacing the existing horizontal-strip cascade atlas with a Virtual Shadow Map (VSM) system: a 16K×16K (or larger) virtual address space backed by demand-paged 128×128 tiles, a clipmap-style projection matrix per shadow caster, page-table-driven sampling, and an explicit fallback that keeps the existing cascade-atlas path available. Planning only — no shader bodies and no page-table backing here.

## Non-goals
- No removal of the existing cascade-atlas shadow path; cascades remain selectable via recipe.
- No directional / point / spot shadow new types beyond what `LightSystem` exposes.
- No shadow-caster filtering changes (PCF, PCSS, ray-traced shadows are separate).
- No per-pixel ray-traced shadows (RT-shadow is reserved as a future GI consumer in `GRAPHICS-046`).
- No CPU-side page allocation; entirely GPU-driven.

## Context
- Owner layer: `graphics/renderer` (page-allocation pass + caster rasterization), `graphics/framegraph` (page-table + page-pool resource lifetime), `graphics/rhi` (sparse-residency surfaces if used; otherwise standard texture array).
- The current cascade atlas allocates fixed-size cascade slabs per light. This wastes memory and resolution at distance and over-allocates at close range. UE5 VSM (Karis et al., SIGGRAPH 2022) demonstrates the modern replacement: a virtual address space where only sampled pages are paged in.
- The VSM approach pairs naturally with Nanite-style cluster culling: only meshlet clusters whose bounds project to allocated pages are rasterized into shadow.
- Cross-links: `GRAPHICS-009` (deferred lighting and shadows), `GRAPHICS-038` (HZB cull at meshlet granularity for shadow casters), `GRAPHICS-044` (meshlet representation feeds caster culling).

## Design decisions to record
1. **Virtual address space shape.** Locked at 16K×16K virtual texels per directional light, organized as a clipmap (multiple resolution levels, each centered on the camera). Record the level count and per-level world extent.
2. **Page size.** 128×128 texels per page (UE5 convention). Record the rule.
3. **Page allocation pass.** A compute pass marks pages required this frame by projecting visible-surface samples through the light projection. Free pages are reclaimed when not marked for K consecutive frames. Record the K policy.
4. **Page pool shape.** A 2D texture atlas (or texture array) holding allocated pages. Decide between sparse residency (`VK_KHR_*sparse*`) and a manually-managed atlas with eviction. Default: manually-managed atlas (portable to non-sparse-capable GPUs).
5. **Page-table format.** R32_UINT per virtual page: high bits = atlas page index, low bits = flags (resident, mapped, free). Record the encoding.
6. **Caster culling.** Each meshlet cluster's bounding sphere is tested per relevant page; only meshlets that touch resident pages are dispatched into shadow rasterization. Reuses the `Pass.Culling` extension from `GRAPHICS-038`.
7. **Page rasterization.** Each resident page is rendered as a render target with the appropriate clipmap projection. Record whether rendering is per-page or per-page-region.
8. **Sampling integration.** Surface shading samples VSM by computing the world-space sample position, looking up the page-table, fetching from the resident atlas. Record the lookup rule and missing-page fallback (sample previous frame or a neutral lit value).
9. **Recipe selection.** `ShadowKind { CascadeAtlas, VirtualShadowMap }` per shadow caster. Default: `CascadeAtlas` until Impl-D ships. Record the rule.
10. **Diagnostics.** `VsmPagesAllocatedCount`, `VsmPagesEvictedCount`, `VsmPageMissCount`, `VsmAtlasPressureFraction`. Atomic counters.
11. **Test split.** `contract;graphics` for page-allocation pass shape, page-table encoding/decoding, caster culling under null RHI; opt-in `gpu;vulkan` smoke for end-to-end VSM golden image.
12. **Layering.** No live ECS access. No new vendor dependencies.

## Required changes
- [ ] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [ ] Cross-link upstream and downstream tasks enumerated in Context.
- [ ] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-047-Impl-A** — Page-table + page-pool resources + frame-graph lifetime + null-RHI shape tests.
- **GRAPHICS-047-Impl-B** — Page-allocation pass + reclamation policy + `contract;graphics` tests.
- **GRAPHICS-047-Impl-C** — Caster culling extension (gated by `GRAPHICS-038` + `GRAPHICS-044`) + diagnostics.
- **GRAPHICS-047-Impl-D** — Surface-shader sampling + recipe selection + integration tests against cascade-atlas baseline.
- **GRAPHICS-047-Impl-E** — Optional sparse-residency backend (gated by `IDevice` capability).

## Tests
- [ ] Planning slice: validators only.
- [ ] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [ ] Update `docs/architecture/rendering-three-pass.md` shadow section.
- [ ] Update `src/graphics/renderer/README.md` shadow-system section.

## Acceptance criteria
- [ ] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [ ] Implementation child slices are identified but not opened.
- [ ] Cascade-atlas path remains the unconditional default until Impl-D ships.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No removal of cascade-atlas path.
- No CPU-side page allocation.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
