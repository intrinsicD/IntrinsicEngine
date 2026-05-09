# GRAPHICS-042 — PBR feature completeness and IBL (planning)

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

## Design decisions to record
1. **BRDF feature set.** Lock the canonical analytic stack: GGX-D + Smith-G + Schlick-F with multi-scatter compensation; Lambert diffuse with Burley retro-reflection; Charlie sheen as opt-in second specular lobe; anisotropic GGX as opt-in roughness-T/B variant; clear-coat as opt-in second GGX lobe over base.
2. **`MaterialParams` extension.** Add optional fields: `SheenColor`, `SheenRoughness`, `AnisotropyT/B/Strength`, `ClearCoatStrength`, `ClearCoatRoughness`. Backward compatible (zero-init = legacy behavior).
3. **Material-type opt-in.** Each new BRDF lobe is gated by a `MaterialTypeDesc` capability flag so the surface shader can compile out unused lobes per material type. Record the gating rule.
4. **Multi-scatter compensation.** Adopt Turquin's energy-compensation lookup (precomputed 2D LUT, R32_FLOAT, axes = roughness × NdotV). Record the LUT generation tool placement.
5. **IBL probe asset shape.** Cube map (RGB16F) at base resolution + prefiltered mip chain (suggested 6 mips) + per-probe BRDF LUT reference. Probes carry position, falloff radius, and clip volumes for parallax correction.
6. **Split-sum integration.** Decide between Karis-style (split LD + DFG LUT) vs. Lazarov-style (analytic DFG fit). Default: Karis split-sum with shared global DFG LUT.
7. **Probe binning.** Reuse the cluster grid from `GRAPHICS-039` for per-cell probe lists. Record the policy for blending multiple overlapping probes (distance falloff with normalized weights).
8. **Skylight / fallback probe.** Reserve slot 0 for a global "infinite distance" sky probe. Record the fallback rule when no probes overlap a cell.
9. **Asset authoring path.** IBL probes are authored through CPU-side `assets` modules; runtime uploads them through `Graphics.GpuAssetCache`. Record the authoring tool placement.
10. **Diagnostics.** `IblProbesLoadedCount`, `IblFallbackUsedCount`. Per-frame counters.
11. **Test split.** `unit` for BRDF energy conservation (white furnace) and Helmholtz reciprocity; `contract;graphics` for cluster-probe binding under null RHI; opt-in `gpu;vulkan` smoke for golden-image probe sampling.
12. **Layering.** `graphics/` consumes precomputed assets; `assets/` owns authoring/transcoding. No live ECS access.

## Required changes
- Capture the design decisions above as explicit recorded answers with trade-off rationales.
- Cross-link upstream and downstream tasks enumerated in Context.
- Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-042-Impl-A** — `MaterialParams` extension + `MaterialTypeDesc` capability flags + null-RHI shape tests.
- **GRAPHICS-042-Impl-B** — Multi-scatter compensation LUT + sheen + anisotropy + clear-coat shader modules.
- **GRAPHICS-042-Impl-C** — IBL probe asset format + `Graphics.GpuAssetCache` upload path + integration tests.
- **GRAPHICS-042-Impl-D** — Cluster-grid probe binning (gated by `GRAPHICS-039`) + integration tests.
- **GRAPHICS-042-Impl-E** — White-furnace + reciprocity unit tests for energy conservation.

## Tests
- Planning slice: validators only.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/rendering-three-pass.md` lighting section with PBR + IBL.
- Update `src/graphics/renderer/README.md` material-system section.
- Update `docs/migration/nonlegacy-parity-matrix.md` PBR rows.

## Acceptance criteria
- Twelve decisions are recorded with explicit answers and trade-off rationales.
- Implementation child slices are identified but not opened.
- Backward compatibility: legacy materials with zero-init new fields render identically.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No GI beyond IBL in this task.
- No SSS/hair/eye/skin BRDFs.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
