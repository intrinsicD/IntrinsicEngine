# GRAPHICS-046 — Hybrid GI: ReSTIR DI/GI hardware path and software fallback (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until `GRAPHICS-045` ships its hardware-RT lifecycle.

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

## Recorded decisions
1. **HW path stages.** Locked stage order: `Pass.GI.RestirDI` → `Pass.GI.RestirGI` → `Pass.GI.SpatialReuse` → `Pass.GI.TemporalReuse` → `Pass.GI.Composite`; reservoir buffers are retained graphics-owned and ping-pong under the retire-deadline pattern. Rationale: fixing the canonical ReSTIR stage order up front lets the frame graph wire dependencies deterministically, and ping-pong retained buffers are required because spatial/temporal reuse reads the prior pass/frame's reservoirs — a transient would be reclaimed too early.
2. **Reservoir buffer shape.** Per-pixel reservoir packs light-id, sample weight, M-count, and target-PDF-hat into an `R32G32B32A32_UINT` texel. Rationale: a single 16-byte RGBA32U texel holds the full reservoir without a struct-of-arrays split, keeping reuse passes a single coherent fetch and matching the canonical ReSTIR reservoir footprint.
3. **Initial sampling source.** Initial light samples are drawn from the clustered light list (`GRAPHICS-039`) plus the HW TLAS for occlusion. Rationale: reusing the already-built cluster light list avoids a second light-acceleration structure and concentrates initial samples on lights that actually affect the froxel, while the TLAS supplies exact visibility — together yielding low-variance initial candidates.
4. **Spatial reuse.** Mitchell-style spatial neighborhood reuse with normal/depth gating, a decreasing radius schedule across iterations, and rejection of neighbors failing the normal/depth similarity test. Rationale: normal/depth gating prevents reuse across geometric discontinuities (the classic ReSTIR bias/light-leak source), and a shrinking radius trades early wide exploration for late local refinement.
5. **Temporal reuse.** Previous-frame reservoirs are reprojected using motion vectors from `GRAPHICS-040`, with a disocclusion fallback to spatial-only reuse where reprojection misses. Rationale: temporal reuse is the dominant variance reducer, so it depends on the existing motion-vector buffer rather than a private one; the disocclusion fallback avoids dragging stale reservoirs into newly revealed pixels.
6. **Software fallback shape.** A DDGI probe grid (sparse 3D irradiance + visibility octahedral-encoded volume) plus per-frame screen-space probes, with a camera-relative grid resolution policy. Rationale: octahedral DDGI is the standard portable diffuse-GI fallback and pairs with screen-space probes for near-field detail; a camera-relative grid keeps probe density where the viewer is rather than over-allocating for whole-scene fixed grids.
7. **Path selection.** A `GiPathKind { Disabled, Software, Hardware }` enum drives recipe selection; `Hardware` falls back to `Software` automatically when `IRayTracingDevice` is unavailable. Rationale: a single explicit enum makes GI selection a recipe decision, and the automatic `Hardware → Software` downgrade guarantees a renderable result on non-RT hardware without the recipe author hardcoding a device assumption.
8. **Compositing.** GI output blends into `SceneColorHDR` after the analytic-lighting surface pass as additive radiance (no overwrite). Rationale: additive radiance composite augments rather than replaces analytic direct lighting (the stated non-goal), and compositing after the surface pass keeps GI a separable contribution that `GiPathKind::Disabled` can skip with zero effect on the base image.
9. **Async-compute affinity.** Spatial/temporal reuse and probe update are tagged `QueueAffinity::AsyncCompute` (gated by `GRAPHICS-037`). Rationale: reuse and probe-update are compute-bound and overlap with raster, so async-compute affinity recovers frame time on multi-queue hardware while the `GRAPHICS-037` partitioner falls back to the graphics queue on single-queue devices.
10. **Denoiser seam.** GI output may pass through a denoiser before composite, reusing the `IReconstructor` shape from `GRAPHICS-040` with a different recipe slot; no vendor denoiser SDK lives in promoted layers. Rationale: reusing the reconstructor seam avoids a parallel denoiser abstraction and keeps vendor middleware (NRD, etc.) behind the existing seam, preserving backend portability.
11. **Diagnostics.** `RestirReservoirOverflowCount`, `DdgiProbeUpdateCount`, `GiFallbackToSoftwareCount`, and `GiFrameTimeMs` are per-frame counters. Rationale: these make reservoir saturation, probe-update churn, the HW→SW downgrade, and GI cost observable without strings, matching the atomic-counter diagnostics convention.
12. **Test split.** `contract;graphics` for path selection, recipe wiring, and reservoir buffer shape under null RHI; opt-in `gpu;vulkan` smoke for ReSTIR DI correctness on a known scene. Rationale: selection/wiring/shape are device-independent and stay on the default CPU gate; only sampled-radiance correctness needs a device, keeping the default gate green on RT-less hosts.
13. **Layering.** No live ECS access; vendor SDKs (RTXDI, FidelityFX-Brixelizer) live behind the same kind of `IReconstructor`-style seam as `GRAPHICS-040` if integrated, never in promoted layers by default. Rationale: preserves AGENTS.md §2 and keeps the promoted GI path backend-portable, with vendor middleware integrated only through an explicit seam.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-046-Impl-A** — `GiPathKind` recipe selection + path-fallback rule + `contract;graphics` tests.
- **GRAPHICS-046-Impl-B** — DDGI probe volume + screen-space probes + software fallback shading kernels.
- **GRAPHICS-046-Impl-C** — ReSTIR DI/GI passes + reservoir buffers + spatial/temporal reuse (gated by `GRAPHICS-045`).
- **GRAPHICS-046-Impl-D** — Composite pass + integration tests against analytic-lighting baseline.
- **GRAPHICS-046-Impl-E** — Optional vendor denoiser hookpoint via `IReconstructor` (one child per vendor; opened only when actually integrated).

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The lighting / GI section of `docs/architecture/rendering-three-pass.md` is deferred to the implementation children (`GRAPHICS-046-Impl-A/C/D`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The GI section of `src/graphics/renderer/README.md` is deferred to the same implementation children for the same reason.
- [x] The GI rows of `docs/migration/nonlegacy-parity-matrix.md` are deferred to the same implementation children for the same reason.

## Acceptance criteria
- [x] Thirteen decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] Software-fallback path is the unconditional default until Impl-C ships.
- [x] Engine renders without GI when `GiPathKind::Disabled`.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All thirteen hybrid-GI decisions are recorded with explicit answers and trade-off rationales: the locked ReSTIR DI→GI→SpatialReuse→TemporalReuse→Composite stage order with retained ping-pong reservoirs, the 16-byte RGBA32U reservoir texel, cluster-light-list + TLAS initial sampling, normal/depth-gated Mitchell spatial reuse with a shrinking radius schedule, motion-vector temporal reuse with disocclusion fallback, the camera-relative DDGI + screen-space-probe software fallback, the `GiPathKind` selector with automatic `Hardware → Software` downgrade, additive-radiance compositing into `SceneColorHDR`, async-compute reuse/probe affinity, the `IReconstructor`-reused denoiser seam, the four per-frame GI counters, the null-RHI contract + opt-in `gpu;vulkan` test split, and the no-live-ECS / vendor-SDK-behind-a-seam layering audit. Implementation children `GRAPHICS-046-Impl-A..E` are identified but not opened; the software fallback stays the unconditional default and `GiPathKind::Disabled` renders unchanged. Per AGENTS.md §9 the architecture-doc/README/parity-matrix updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No vendor SDK imports in promoted graphics layers.
- No removal of analytic direct lighting.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
