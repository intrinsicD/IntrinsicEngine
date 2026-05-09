# GRAPHICS-043 — Visibility buffer recipe and deferred materialization (planning)

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

## Design decisions to record
1. **Vis-buffer format.** Locked R32G32_UINT: high bits = instance-id (24) + meshlet/cluster-id (8); low bits = triangle-id within meshlet (24) + reserved flags (8). Plus separate `R32_SFLOAT` depth. Record exact bit layout.
2. **8-bucket interaction.** Vis-buffer recipe consumes `SurfaceOpaque` + `SurfaceAlphaMask` lanes. Lines, points, selection, and shadow lanes are unchanged. Record the rule that vis-buffer is per-recipe, not per-bucket.
3. **Material-id table.** A persistent GPU-resident table keyed by instance-id resolves to a material-type id used as a sort key for materialization. Record the buffer ownership.
4. **Materialization pass shape.** One compute dispatch *per material type* over its set of pixels, found via prefix-sum tile classification. Each dispatch reads the vis-buffer, reconstructs barycentrics + ddx/ddy, samples bindless textures, writes `SceneColorHDR`. Record the tile classification policy.
5. **Per-material kernel sourcing.** Each `MaterialTypeDesc` provides a Slang module that compiles to a materialization kernel (gated by `GRAPHICS-041`). Record the naming convention.
6. **Barycentric reconstruction.** Decide between (a) hardware `BaryCoordKHR` if available, (b) software reconstruction from triangle vertex positions reprojected via index buffer + instance transform. Default: software reconstruction (portable). Record the rule.
7. **Analytic ddx/ddy.** Differentials are reconstructed analytically from neighboring pixels' barycentrics. Record the rule for edge cases (cross-triangle quad helpers).
8. **Light + shadow integration.** Materialization kernels read the same clustered light list (`GRAPHICS-039`), shadow atlas (`GRAPHICS-047` when ready), and IBL probes (`GRAPHICS-042`). Record that the lighting code is shared between forward, classic-deferred, and vis-buffer recipes via a Slang module.
9. **Recipe selection.** `FrameRecipeKind::VisBufferDeferredMaterialization` is the new opt-in. Default recipe is unchanged. Record the rule.
10. **Diagnostics.** `VisBufferPixelsPerMaterial[]`, `VisBufferOverdrawCount` (always low — that's the point), `MaterializationKernelDispatchCount`. Atomic increments.
11. **Test split.** `contract;graphics` for vis-buffer encoding/decoding, tile classification, kernel dispatch shape under null RHI; opt-in `gpu;vulkan` smoke for shading correctness against the forward recipe.
12. **Layering.** No live ECS. No new RHI surfaces beyond what the existing framegraph supports.

## Required changes
- Capture the design decisions above as explicit recorded answers with trade-off rationales.
- Cross-link upstream and downstream tasks enumerated in Context.
- Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-043-Impl-A** — Vis-buffer pass + ID encoding + `contract;graphics` decode tests.
- **GRAPHICS-043-Impl-B** — Material-id table + tile classification + null-RHI tests.
- **GRAPHICS-043-Impl-C** — Materialization kernel dispatch wiring + Slang generic per material type (gated by `GRAPHICS-041`).
- **GRAPHICS-043-Impl-D** — Recipe selection + integration tests against forward-recipe golden image.
- **GRAPHICS-043-Impl-E** — Opt-in `gpu;vulkan` smoke shading-equivalence test.

## Tests
- Planning slice: validators only.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/rendering-three-pass.md` with the vis-buffer recipe and decoder spec.
- Update `src/graphics/renderer/README.md` material-system + recipe section.

## Acceptance criteria
- Twelve decisions are recorded with explicit answers and trade-off rationales.
- Implementation child slices are identified but not opened.
- Forward and classic-deferred recipes remain default and unchanged.
- 8-bucket lane contract preserved.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No removal of forward / classic-deferred recipes.
- No 8-bucket renumbering.
- No Nanite-style cluster DAG in this task.
- No transparent material coverage.
- No mixing of mechanical file moves with semantic refactors.
