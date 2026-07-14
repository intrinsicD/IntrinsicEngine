# GRAPHICS-043 — Visibility buffer recipe and deferred materialization (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until Theme A is complete.

## Goal
Lock down the contract for an alternative surface-rendering recipe that writes a visibility buffer (instance-id + meshlet/triangle-id + depth) instead of a multi-target G-buffer, then materializes shading in compute kernels keyed on material type with bindless texture access and barycentric reconstruction. Planning only — no shader bodies and no opt-in switch in the default recipe land here.

## Non-goals
- No removal of the existing forward and classic-deferred recipes; vis-buffer is a **third** recipe selected per render target.
- No virtualized-geometry / cluster DAG (`GRAPHICS-056`).
- No Nanite parity goals.
- No transparent / alpha-blended surface coverage; vis-buffer covers opaque + alpha-mask only.
- No CPU-side material sort.

## Context
- Owner layer: `graphics/renderer` (vis-buffer pass + per-material compute kernels), `graphics/framegraph` (vis-buffer + lit-target lifetime), `graphics/rhi` (existing storage-image / storage-buffer surfaces).
- Vis-buffer + deferred materialization (Burns/Hunt 2013, Wicked Engine, Filmic Worlds, Nanite) eliminates G-buffer overdraw and bandwidth, and scales to thousands of materials with bindless. Trade-off: heavy compute material sort, harder partial-coverage MSAA, more complex barycentric reconstruction.
- Cross-links: `GRAPHICS-008` (G-buffer recipe stays), `GRAPHICS-041` (Slang modules per material type are the natural carrier), `GRAPHICS-044` (meshlet representation feeds the vis-buffer ID encoding), `GRAPHICS-046` (GI samples shaded scene color regardless of recipe).

## Recorded decisions
1. **Vis-buffer format.** Locked **`R32G32_UINT`**: word 0 = `instance-id (24 bits) | meshlet-id (8 bits)`; word 1 = `triangle-id within meshlet (24 bits) | reserved flags (8 bits)`. Separate **`R32_SFLOAT` depth** target (no packing depth into the UINT). Rationale: 24/8 instance/meshlet split matches the `GRAPHICS-044` 256-meshlets-per-instance cap exactly; a separate float depth keeps HZB/Z-test paths unchanged and avoids precision loss from bit-packing depth.
2. **8-bucket interaction.** The vis-buffer recipe consumes the **`SurfaceOpaque` + `SurfaceAlphaMask`** lanes only; the **lines, points, selection, and shadow lanes are unchanged**. Vis-buffer is selected **per-recipe, not per-bucket** — the 8-bucket cull contract from `GRAPHICS-007` is untouched. Rationale: scoping the recipe to opaque/alpha-mask preserves the existing lane numbering (acceptance criterion) and lets debug/selection/shadow passes run identically regardless of surface recipe.
3. **Material-id table.** A **persistent GPU-resident table keyed by instance-id → material-type-id**, owned by the renderer (built from the extracted `RenderWorld` instance list, refreshed when residency changes), used as the materialization sort key. Rationale: a persistent instance→material table avoids re-deriving material types per frame and gives the tile-classification pass a stable sort key.
4. **Materialization pass shape.** **One compute dispatch per material type** over that type's pixel set, located via a **prefix-sum tile-classification pass** (count pixels per material per tile → scan → compact pixel lists). Each dispatch reads the vis-buffer, reconstructs barycentrics + ddx/ddy, samples bindless textures, and writes `SceneColorHDR`. Rationale: prefix-sum tile classification is the canonical vis-buffer materialization scheduler (Wicked/Filmic); per-material dispatch keeps each kernel branch-free and bindless-friendly.
5. **Per-material kernel sourcing.** Each `MaterialTypeDesc` provides a **Slang materialization module** (gated by `GRAPHICS-041`) compiled to a kernel named **`Materialize_<MaterialTypeName>`**, cached in `Graphics.GpuAssetCache` by `(MaterialTypeID, compiler-version-hash)`. Rationale: reuses the `GRAPHICS-041` generic/specialization + caching machinery so vis-buffer materialization shares the surface BRDF code rather than forking it.
6. **Barycentric reconstruction.** Default **software reconstruction** from the triangle's vertex positions (fetched via the meshlet vertex-range + local index tables) reprojected through the instance transform + camera; hardware `BaryCoordKHR` is recorded as an **optional fast path gated by `IDevice` capability**. Rationale: software reconstruction is portable across all backends (the engine's null/CPU-testable discipline) and is exact; the hardware path is a later opt-in optimization.
7. **Analytic ddx/ddy.** Texture differentials are reconstructed **analytically** from the per-pixel barycentrics of the same triangle at neighboring pixels (2×2 quad), with **cross-triangle quad helpers clamped** to the center pixel's triangle (a helper that lands on a different triangle reuses the center barycentric gradient rather than producing a discontinuity). Rationale: analytic differentials avoid the hardware-derivative discontinuity at triangle edges that plagues vis-buffer materialization; clamping cross-triangle helpers keeps mip selection stable along silhouettes.
8. **Light + shadow integration.** Materialization kernels read the **same clustered light list (`GRAPHICS-039`)**, **shadow atlas (`GRAPHICS-047` when ready, today's cascade atlas until then)**, and **IBL probes (`GRAPHICS-042`)**; the lighting/shadow/IBL code is a **shared Slang module** imported identically by forward, classic-deferred, and vis-buffer recipes. Rationale: sharing one lighting module guarantees the three recipes shade identically (the golden-image acceptance target) and prevents lighting drift between paths.
9. **Recipe selection.** A **future explicit recipe selector/API** chooses `VisBufferDeferredMaterialization`; the **default recipe is unchanged** and the selector does **not** depend on the retired `RenderConfig::FrameRecipe` field — it is an explicit recipe-build input mirroring the `GRAPHICS-040` AA selector. Rationale: keeping selection an explicit build input (not a resurrected config enum) honors the retired-selector decision and keeps the default path untouched.
10. **Diagnostics.** **`VisBufferPixelsPerMaterial[]`** (per-material pixel count), **`VisBufferOverdrawCount`** (expected near-zero — the point of the technique), and **`MaterializationKernelDispatchCount`**. Atomic increments; no per-frame strings. Rationale: per-material pixel counts surface materialization load distribution; near-zero overdraw is the observable success signal versus the G-buffer path.
11. **Test split.** **`contract;graphics`** for vis-buffer encode/decode round-trip, tile-classification prefix-sum shape, and kernel-dispatch-count shape under null RHI; **opt-in `gpu;vulkan`** smoke for shading correctness against the forward recipe (golden-image equivalence). Rationale: encode/decode and the classification scheduler are pure-CPU contracts; only final shading equivalence needs a device.
12. **Layering.** **No live ECS** (instances arrive through the extracted `RenderWorld`); **no new RHI surfaces** beyond the existing storage-image/storage-buffer framegraph resources. Vis-buffer passes + materialization kernels live in `graphics/renderer`. Rationale: preserves AGENTS.md §2 — graphics consumes the snapshot and reuses existing RHI surfaces, no upward edges.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-043-Impl-A** — Vis-buffer pass + ID encoding + `contract;graphics` decode tests.
- **GRAPHICS-043-Impl-B** — Material-id table + tile classification + null-RHI tests.
- **GRAPHICS-043-Impl-C** — Materialization kernel dispatch wiring + Slang generic per material type (gated by `GRAPHICS-041`).
- **GRAPHICS-043-Impl-D** — Recipe selection + integration tests against forward-recipe golden image.
- **GRAPHICS-043-Impl-E** — Opt-in `gpu;vulkan` smoke shading-equivalence test.

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The vis-buffer recipe + decoder-spec section for `docs/architecture/rendering-three-pass.md` is deferred to the implementation children (`GRAPHICS-043-Impl-A..E`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The material-system + recipe section of `src/graphics/renderer/README.md` is deferred to the same implementation children for the same reason.

## Acceptance criteria
- [x] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] Forward and classic-deferred recipes remain default and unchanged.
- [x] 8-bucket lane contract preserved.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All twelve visibility-buffer decisions are recorded with explicit answers and trade-off rationales: the `R32G32_UINT` instance(24)/meshlet(8)/triangle(24)/flags(8) encoding aligned to the GRAPHICS-044 256-meshlets cap with a separate `R32_SFLOAT` depth, the per-recipe (not per-bucket) `SurfaceOpaque`+`SurfaceAlphaMask` scope preserving the 8-bucket contract, the persistent instance→material-type table, the prefix-sum tile-classification + per-material compute dispatch shape, the `Materialize_<MaterialTypeName>` Slang kernels cached via GRAPHICS-041, the portable software barycentric reconstruction with an optional hardware-`BaryCoordKHR` fast path, the analytic ddx/ddy with cross-triangle quad-helper clamping, the shared lighting/shadow/IBL Slang module across all three recipes, the explicit non-`RenderConfig::FrameRecipe` recipe selector, the three diagnostics counters, the encode/decode + classification contract + golden-image gpu smoke test split, and the layering audit. Implementation children `GRAPHICS-043-Impl-A..E` are identified but not opened; forward and classic-deferred recipes stay the unchanged default and the 8-bucket lane contract is preserved. Both upstream planning gates GRAPHICS-041 (Slang) and GRAPHICS-044 (meshlets) are retired to `tasks/done/`. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No removal of forward / classic-deferred recipes.
- No 8-bucket renumbering.
- No Nanite-style cluster DAG in this task.
- No transparent material coverage.
- No mixing of mechanical file moves with semantic refactors.
