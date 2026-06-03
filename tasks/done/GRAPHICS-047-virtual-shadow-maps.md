# GRAPHICS-047 — Virtual Shadow Maps to replace cascade atlas (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until the meshlet caster-cull prerequisites (`GRAPHICS-038`/`GRAPHICS-044`) feed an implementation slice.

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

## Recorded decisions
1. **Virtual address space shape.** Locked at 16K×16K virtual texels per directional light, organized as a clipmap of multiple resolution levels each centered on the camera, with a recorded per-level world extent doubling per level. Rationale: a clipmap matches shadow detail to screen-space density (high resolution near the camera, coarse far away) without the cascade atlas's fixed slabs, and 16K virtual texels gives ample address space because only sampled pages are ever backed.
2. **Page size.** 128×128 texels per page (UE5 convention). Rationale: 128² is the empirically validated VSM page granularity — small enough that unsampled regions cost nothing, large enough that page-table indirection and per-page draw setup stay amortized.
3. **Page allocation pass.** A compute pass marks pages required this frame by projecting visible-surface samples through the light projection; free pages are reclaimed when not marked for K consecutive frames, with K recorded as a small hysteresis constant (default 4). Rationale: marking from actual visible samples allocates exactly the pages shading will read, and a K-frame reclaim hysteresis avoids thrashing pages in and out under small camera motion.
4. **Page pool shape.** A manually-managed 2D atlas (or texture array) holding allocated pages with explicit eviction, chosen over sparse residency (`VK_KHR_*sparse*`) as the default. Rationale: a manually-managed atlas is portable to non-sparse-capable GPUs and keeps eviction policy in engine code; sparse residency is recorded as an optional backend (`Impl-E`) for hardware that supports it, not a baseline requirement.
5. **Page-table format.** `R32_UINT` per virtual page: high bits = atlas page index, low bits = flags (resident, mapped, free). Rationale: a single 32-bit indirection per virtual page is the minimal lookup payload, packs index + state without a second table, and is trivially atomically updatable by the allocation pass.
6. **Caster culling.** Each meshlet cluster's bounding sphere is tested per relevant page; only meshlets that touch resident pages are dispatched into shadow rasterization, reusing the `Pass.Culling` extension from `GRAPHICS-038`. Rationale: page-aware caster culling is the core VSM efficiency win — it bounds shadow rasterization to geometry that actually feeds a backed page — and reusing the meshlet cull avoids a second caster-culling implementation.
7. **Page rasterization.** Each resident page is rendered with the appropriate clipmap projection; rendering is per-page-region (batched contiguous resident pages sharing a projection) rather than strictly per-page. Rationale: per-page-region batching amortizes draw-setup and renderpass overhead across adjacent resident pages while keeping each region's projection well-defined, avoiding a draw call per 128² tile.
8. **Sampling integration.** Surface shading computes the world-space sample position, looks up the page-table, and fetches from the resident atlas; a missing page falls back to a neutral lit value (treat as unshadowed) rather than sampling stale data. Rationale: the page-table indirection is the VSM sampling contract, and a missing-page-as-unshadowed fallback fails bright (no spurious dark artifacts) while the allocation pass converges, which is the least-surprising transient behavior.
9. **Recipe selection.** `ShadowKind { CascadeAtlas, VirtualShadowMap }` per shadow caster, defaulting to `CascadeAtlas` until `Impl-D` ships. Rationale: a per-caster enum lets VSM adoption be incremental and keeps the proven cascade-atlas path as the unconditional default, so no scene regresses before the VSM sampling path is integration-tested.
10. **Diagnostics.** `VsmPagesAllocatedCount`, `VsmPagesEvictedCount`, `VsmPageMissCount`, and `VsmAtlasPressureFraction` are atomic counters. Rationale: these surface allocation churn, miss rate (sampling pages the allocation pass did not back), and atlas saturation — the signals needed to tune K and atlas size — without strings or readback.
11. **Test split.** `contract;graphics` for page-allocation pass shape, page-table encode/decode, and caster culling under null RHI; opt-in `gpu;vulkan` smoke for an end-to-end VSM golden image. Rationale: the page-table math and pass wiring are device-independent and stay on the CPU gate; only the rasterized shadow result needs a device, keeping the default gate green.
12. **Layering.** No live ECS access and no new vendor dependencies. Rationale: preserves AGENTS.md §2 — the VSM system consumes the shadow-caster snapshot and owns only GPU resources, with no live-scene access and no middleware.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-047-Impl-A** — Page-table + page-pool resources + frame-graph lifetime + null-RHI shape tests.
- **GRAPHICS-047-Impl-B** — Page-allocation pass + reclamation policy + `contract;graphics` tests.
- **GRAPHICS-047-Impl-C** — Caster culling extension (gated by `GRAPHICS-038` + `GRAPHICS-044`) + diagnostics.
- **GRAPHICS-047-Impl-D** — Surface-shader sampling + recipe selection + integration tests against cascade-atlas baseline.
- **GRAPHICS-047-Impl-E** — Optional sparse-residency backend (gated by `IDevice` capability).

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The shadow section of `docs/architecture/rendering-three-pass.md` is deferred to the implementation children (`GRAPHICS-047-Impl-A/B/D`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The shadow-system section of `src/graphics/renderer/README.md` is deferred to the same implementation children for the same reason.

## Acceptance criteria
- [x] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] Cascade-atlas path remains the unconditional default until Impl-D ships.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All twelve VSM decisions are recorded with explicit answers and trade-off rationales: the 16K² camera-centered clipmap virtual address space, the 128² page size, the visible-sample-driven allocation pass with a K-frame reclaim hysteresis, the portable manually-managed atlas (sparse residency optional), the `R32_UINT` index+flags page-table format, the meshlet-cluster page-aware caster culling reusing `GRAPHICS-038`, per-page-region rasterization, page-table sampling with a fail-bright missing-page fallback, the `ShadowKind` per-caster selector defaulting to `CascadeAtlas`, the four atomic VSM counters, the null-RHI contract + opt-in `gpu;vulkan` golden-image test split, and the no-live-ECS / no-vendor-dependency layering audit. Implementation children `GRAPHICS-047-Impl-A..E` are identified but not opened; the cascade-atlas path stays the unconditional default and no page-table backing or shader bodies land. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No removal of cascade-atlas path.
- No CPU-side page allocation.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
