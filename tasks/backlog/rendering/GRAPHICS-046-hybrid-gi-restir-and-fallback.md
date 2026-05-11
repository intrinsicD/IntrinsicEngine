# GRAPHICS-046 — Hybrid GI: ReSTIR DI/GI hardware path and software fallback (planning)

## Goal
Lock down the contract for a hybrid global illumination system providing (a) a hardware path using ReSTIR DI for direct lighting and ReSTIR GI for indirect, on top of `IRayTracingDevice` from `GRAPHICS-045`, and (b) a software fallback combining a probe volume (DDGI-style) with screen-space probes when RT is unavailable. Planning only — no shader bodies, no neural denoiser hookpoint here.

## Non-goals
- No path tracer / reference mode (reserved for a separate task if needed).
- No neural radiance cache (covered by `GRAPHICS-049`).
- No neural denoiser integration body — it plugs into the `IReconstructor` seam from `GRAPHICS-040`.
- No removal of analytic direct lighting; ReSTIR DI augments rather than replaces.
- No mobile fallback design; mobile RT support is out of scope.

## Context
- Owner layer: `graphics/renderer` (ReSTIR passes + probe volume + reservoir buffers), `graphics/rhi` (no surface change beyond `IRayTracingDevice`), `graphics/framegraph` (reservoir + probe-volume resource lifetime).
- ReSTIR (Bitterli et al. 2020) and successors (ReSTIR GI 2021, ReSTIR PT 2022, ReSTIR PT Enhanced 2025) are the canonical real-time RT GI sampling pattern, shipping in Cyberpunk Overdrive, Alan Wake 2, Indiana Jones, Portal RTX.
- Software fallback: DDGI (NVIDIA 2019, McGuire et al.) for sparse irradiance probes + screen-space probes for high-frequency near-field GI.
- Cross-links: `GRAPHICS-040` (denoiser plugs into reconstructor seam), `GRAPHICS-042` (IBL provides skylight; GI augments), `GRAPHICS-045` (HW RT prerequisite), `GRAPHICS-049` (NRC slot in the same path), `GRAPHICS-039` (clustered light list feeds initial light samples).

## Design decisions to record
1. **HW path stages.** Locked stage order: `Pass.GI.RestirDI` → `Pass.GI.RestirGI` → `Pass.GI.SpatialReuse` → `Pass.GI.TemporalReuse` → `Pass.GI.Composite`. Reservoir buffers retained graphics-owned, ping-pong with retire-deadline.
2. **Reservoir buffer shape.** Per-pixel reservoir: light-id, sample weight, M-count, target-PDF-hat. Layout `R32G32B32A32_UINT` packed. Record the encoding rule.
3. **Initial sampling source.** Initial light samples drawn from the clustered light list (`GRAPHICS-039`) plus the HW TLAS for occlusion. Record the rule.
4. **Spatial reuse.** Mitchell-style spatial neighborhood reuse with normal/depth gating. Record the radius schedule and rejection criteria.
5. **Temporal reuse.** Reproject previous-frame reservoir using motion vectors from `GRAPHICS-040`. Record the disocclusion fallback.
6. **Software fallback shape.** DDGI probe grid (sparse 3D probe volume of irradiance + visibility octahedral encoding) + per-frame screen-space probes. Decide grid resolution policy (per-scene fixed, or per-camera-relative).
7. **Path selection.** A `GiPathKind { Disabled, Software, Hardware }` enum drives recipe selection. `Hardware` falls back to `Software` automatically when `IRayTracingDevice` is unavailable. Record the rule.
8. **Compositing.** GI output blends into `SceneColorHDR` after the analytic-lighting surface pass. Record the blend rule (additive radiance, no overwrite).
9. **Async-compute affinity.** Spatial/temporal reuse and probe update tagged with `QueueAffinity::AsyncCompute` (gated by `GRAPHICS-037`).
10. **Denoiser seam.** GI output may pass through a denoiser before composite. Reuses the `IReconstructor` shape from `GRAPHICS-040` with a different recipe slot. Record the rule that no vendor denoiser SDK lives in promoted layers.
11. **Diagnostics.** `RestirReservoirOverflowCount`, `DdgiProbeUpdateCount`, `GiFallbackToSoftwareCount`, `GiFrameTimeMs`. Per-frame counters.
12. **Test split.** `contract;graphics` for path selection, recipe wiring, reservoir buffer shape, all under null RHI; opt-in `gpu;vulkan` smoke for ReSTIR DI correctness on a known scene.
13. **Layering.** No live ECS. Vendor SDKs (RTXDI, FidelityFX-Brixelizer) live behind the same kind of `IReconstructor`-style seam as `GRAPHICS-040` if integrated; not in promoted layers by default.

## Required changes
- [ ] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [ ] Cross-link upstream and downstream tasks enumerated in Context.
- [ ] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-046-Impl-A** — `GiPathKind` recipe selection + path-fallback rule + `contract;graphics` tests.
- **GRAPHICS-046-Impl-B** — DDGI probe volume + screen-space probes + software fallback shading kernels.
- **GRAPHICS-046-Impl-C** — ReSTIR DI/GI passes + reservoir buffers + spatial/temporal reuse (gated by `GRAPHICS-045`).
- **GRAPHICS-046-Impl-D** — Composite pass + integration tests against analytic-lighting baseline.
- **GRAPHICS-046-Impl-E** — Optional vendor denoiser hookpoint via `IReconstructor` (one child per vendor; opened only when actually integrated).

## Tests
- [ ] Planning slice: validators only.
- [ ] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [ ] Update `docs/architecture/rendering-three-pass.md` lighting / GI section.
- [ ] Update `src/graphics/renderer/README.md` GI section.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` GI rows.

## Acceptance criteria
- [ ] Thirteen decisions are recorded with explicit answers and trade-off rationales.
- [ ] Implementation child slices are identified but not opened.
- [ ] Software-fallback path is the unconditional default until Impl-C ships.
- [ ] Engine renders without GI when `GiPathKind::Disabled`.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No vendor SDK imports in promoted graphics layers.
- No removal of analytic direct lighting.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
