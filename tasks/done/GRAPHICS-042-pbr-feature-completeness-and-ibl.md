# GRAPHICS-042 — PBR feature completeness and IBL (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until Theme A is complete.

## Goal
Lock down the contract for bringing the analytic surface BRDF up to modern PBR feature parity (energy-conserving GGX with multi-scatter compensation, sheen via the Charlie distribution, anisotropic GGX, optional clear-coat) and adding split-sum image-based lighting (prefiltered environment + BRDF integration LUT). Planning only — no shader bodies or material-graph extensions land here.

## Non-goals
- No global illumination beyond IBL + shadow occlusion (`GRAPHICS-046` covers RT/probe GI).
- No subsurface scattering, hair, eye, or skin BRDFs (future work, not in this roadmap).
- No transparent/special-material BRDFs beyond opaque dielectric/metal (`GRAPHICS-025` owns transparency).
- No new material classification beyond what `MaterialSystem` already exposes.
- No CPU-side IBL prefiltering at runtime; precomputed assets only in this slice.

## Context
- Owner layer: `graphics/renderer` (BRDF kernels), `graphics/assets` (IBL probe assets), `assets` (CPU-side probe authoring).
- Today the default StandardPBR material implements a baseline GGX + Lambert. Energy compensation, sheen, anisotropy, and clear-coat are absent. IBL is not present.
- Modern PBR feature targets: GGX with multi-scatter compensation (Turquin / Heitz), Charlie sheen (Conty/Kulla), anisotropic GGX (Burley/Disney), clear-coat (Burley/Disney/UE), split-sum IBL (Karis / Lazarov).
- Cross-links: `GRAPHICS-006` (material registry must support new params), `GRAPHICS-009` (deferred lighting consumer), `GRAPHICS-039` (clustered probe binning shares the cell grid), `GRAPHICS-041` (Slang modules carry BRDFs).

## Recorded decisions
1. **BRDF feature set.** Locked canonical analytic stack: **GGX-D + Smith height-correlated G + Schlick-F** specular with multi-scatter energy compensation; **Burley-retro-reflection Lambert** diffuse; **Charlie sheen** as an opt-in second specular lobe (Conty/Kulla); **anisotropic GGX** as an opt-in roughness-T/B variant (Disney); **clear-coat** as an opt-in second GGX lobe over the base. Base path (no opt-ins) is the energy-compensated dielectric/metal default. Rationale: this is the de-facto glTF/UE/Filament feature parity set; making sheen/anisotropy/clear-coat opt-in lobes keeps the common material cheap.
2. **`MaterialParams` extension.** Add **optional, zero-init-legacy** fields: `SheenColor`, `SheenRoughness`, `AnisotropyT`, `AnisotropyB`, `AnisotropyStrength`, `ClearCoatStrength`, `ClearCoatRoughness`. Zero-init reproduces today's behavior bit-for-bit (no lobe enabled). Rationale: additive zero-default fields preserve backward compatibility (acceptance criterion) and keep the `MaterialBuffer` SSBO layout append-only.
3. **Material-type opt-in.** Each new lobe is gated by a **`MaterialTypeDesc` capability flag** (`HasSheen`, `HasAnisotropy`, `HasClearCoat`) so the surface shader **compiles out** unused lobes per material type (a Slang specialization constant from `GRAPHICS-041`). Rationale: per-type compile-out avoids paying for lobes a material never uses and keeps the hot path branch-free.
4. **Multi-scatter compensation.** Adopt **Turquin's energy-compensation** via a precomputed **2D LUT (`R32_FLOAT`, axes = roughness × NdotV)** giving the directional albedo `E(µ)`; the shader applies the multiplicative compensation term. The LUT generator is an **offline tool under `tools/`** (mirroring the Slang/IBL bake tools), shipped as a cached asset. Rationale: Turquin's single-LUT method is the cheapest correct multi-scatter approximation; an offline LUT keeps the runtime free of a furnace-integration step.
5. **IBL probe asset shape.** **Cube map (RGB16F)** at base resolution + **prefiltered specular mip chain (6 mips)** + a reference to the shared **DFG BRDF LUT**; each probe carries **position, falloff radius, and a parallax-correction clip volume (AABB/OBB)**. Rationale: 6-mip prefiltered RGB16F is the standard split-sum specular representation; per-probe falloff + clip volume enables local parallax-corrected reflections without a full GI solution.
6. **Split-sum integration.** Default **Karis split-sum**: separate prefiltered radiance (LD term) × a **shared global DFG LUT** (`RG16F`, axes = roughness × NdotV). Lazarov's analytic DFG fit is rejected as the default (small bias on rough metals) but recorded as an optional shader-side fast path. Rationale: Karis split-sum with a real DFG LUT is the quality baseline; the analytic fit stays available where LUT bandwidth matters.
7. **Probe binning.** Reuse the **`GRAPHICS-039` cluster grid** for per-cell probe lists via the parallel probe-index buffer that task reserved. Multiple overlapping probes blend by **distance falloff with normalized weights** (inverse-distance within each probe's falloff radius, renormalized per pixel). Rationale: sharing the froxel grid avoids a second spatial structure; normalized falloff weighting gives smooth probe transitions without popping.
8. **Skylight / fallback probe.** **Slot 0 is reserved for a global "infinite distance" sky probe**; when no local probe overlaps a cell the shader falls back to slot 0, and `IblFallbackUsedCount` is incremented. Rationale: a guaranteed sky probe makes IBL always-defined (no black reflections) and gives a single observable fallback path.
9. **Asset authoring path.** IBL probes are authored through **CPU-side `assets` modules** (cube-map capture/prefilter + clip-volume metadata); runtime uploads them through **`Graphics.GpuAssetCache`**. The prefilter/DFG bake tool lives under **`tools/`**. Rationale: keeps authoring/transcoding in `assets` (CPU-only) and upload in graphics, honoring AGENTS.md §2; the bake tool stays offline like the multi-scatter LUT.
10. **Diagnostics.** Per-frame **`IblProbesLoadedCount`** and **`IblFallbackUsedCount`** counters surfaced through the renderer light/material diagnostics. Scalar/atomic only; no per-frame strings. Rationale: matches existing diagnostics discipline and makes probe residency + fallback usage observable.
11. **Test split.** **`unit`** for BRDF energy conservation (white-furnace ≤ 1.0 ∀ roughness/NdotV) and Helmholtz reciprocity; **`contract;graphics`** for cluster-probe binding + slot-0 fallback under null RHI; **opt-in `gpu;vulkan`** smoke for golden-image probe sampling. Rationale: energy/reciprocity are pure-math CPU invariants (the strongest correctness signal for a BRDF); only sampling output needs a device.
12. **Layering.** `graphics/` **consumes precomputed assets**; `assets/` owns authoring/transcoding; the bake/LUT tools live under `tools/`. No live ECS access (probes arrive as graphics-owned uploaded assets). Rationale: preserves AGENTS.md §2 — assets are CPU-only, graphics never owns file IO or runtime prefiltering.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-042-Impl-A** — `MaterialParams` extension + `MaterialTypeDesc` capability flags + null-RHI shape tests.
- **GRAPHICS-042-Impl-B** — Multi-scatter compensation LUT + sheen + anisotropy + clear-coat shader modules.
- **GRAPHICS-042-Impl-C** — IBL probe asset format + `Graphics.GpuAssetCache` upload path + integration tests.
- **GRAPHICS-042-Impl-D** — Cluster-grid probe binning (gated by `GRAPHICS-039`) + integration tests.
- **GRAPHICS-042-Impl-E** — White-furnace + reciprocity unit tests for energy conservation.

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The PBR + IBL lighting section for `docs/architecture/rendering-three-pass.md` is deferred to the implementation children (`GRAPHICS-042-Impl-A..E`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The material-system section of `src/graphics/renderer/README.md` is deferred to the same implementation children for the same reason.
- [x] The `docs/migration/nonlegacy-parity-matrix.md` PBR rows update is deferred to the implementation children for the same reason.

## Acceptance criteria
- [x] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] Backward compatibility: legacy materials with zero-init new fields render identically.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All twelve PBR/IBL decisions are recorded with explicit answers and trade-off rationales: the GGX-D + height-correlated Smith-G + Schlick-F + multi-scatter base with opt-in Charlie sheen / anisotropic GGX / clear-coat lobes, the zero-init-legacy `MaterialParams` extension, the `MaterialTypeDesc` capability-flag compile-out, Turquin's `R32_FLOAT` multi-scatter LUT from an offline `tools/` generator, the RGB16F cube-map + 6-mip prefiltered + parallax-clip-volume probe shape, Karis split-sum with a shared `RG16F` DFG LUT, GRAPHICS-039 cluster-grid probe binning with normalized falloff blending, the slot-0 sky-probe fallback, the `assets`-authored / `Graphics.GpuAssetCache`-uploaded path, the two IBL counters, the white-furnace/reciprocity unit + cluster-probe contract + gpu smoke test split, and the layering audit. Implementation children `GRAPHICS-042-Impl-A..E` are identified but not opened; backward compatibility is preserved by zero-init defaults and no new material classification lands. Per AGENTS.md §9 the architecture-doc/README/parity-matrix updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No GI beyond IBL in this task.
- No SSS/hair/eye/skin BRDFs.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
