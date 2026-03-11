# IntrinsicEngine — Architecture TODOs (Living Document)

This document tracks the **active rendering-architecture backlog** for IntrinsicEngine.

**Policy:** completed/refactored work does **not** stay here. We rely on Git history for the past and keep this file focused on what remains.

---

## 0. Scope & Success Criteria

**Current focus:** finish the resource-driven rendering refactor without painting the engine into a corner for deferred, hybrid, transparency, or material-system work.

**Success criteria:**

- Deterministic frame construction with explicit pass/resource dependencies.
- Canonical render targets with validated producer/consumer contracts.
- Selection, post, and debug visualization decoupled from a single lighting path.
- Only recipe-required intermediate resources allocated per frame.
- Migration hardened by graph compile tests, contract tests, and at least one integration test.

---

## 1. Related Documents

- `docs/architecture/rendering-three-pass.md` — canonical runtime rendering architecture spec (pass contracts, data contracts, invariants).
- `PLAN.md` — archival index for the completed three-pass migration.
- `ROADMAP.md` — medium/long-horizon feature roadmap and phase ordering.
- `README.md` — user-facing architecture summary, build/test entry points, and SLOs.
- `CLAUDE.md` — contributor conventions, C++23 policy, and markdown sync contract.

---

## P0 — Critical Refactor

Stage 1 (file extraction), Stage 2 (module migration), and Stage 3 (stability tests) are complete — see git history.

## 2. Next (P1) — Near-Term Follow-Up After the Refactor Lands

These are not required to finish the first wave, but they should begin soon after P0 is stable.

### B1. Deferred Lighting Path

- [ ] Add a proper deferred lighting composition path.
- [ ] Define the minimum G-buffer layout.
- [ ] Route opaque lighting through the deferred path.
- [ ] Preserve the forward path for unsupported cases.
- [ ] Keep selection/debug independent of the deferred path.

### B2. Hybrid Renderer Support

- [ ] Support forward + deferred coexistence.
- [ ] Define which primitives/materials are deferred-capable.
- [ ] Keep transparent/special/debug rendering in the forward path.
- [ ] Define composition rules between paths.

### B3. Selection and Editor Tooling Improvements

- [ ] Improve primitive/submesh selection.
- [ ] Add mask visualization in the editor.

---

## 3. Later (P2) — Planned Downstream Work

These items should be **planned now** so the current refactor leaves room for them, but they should be implemented later.

### C1. Material System Rewrite

- [ ] Define future material data-model requirements.
- [ ] Decide how materials map to:
  - [ ] Forward path.
  - [ ] Deferred path.
  - [ ] Hybrid path.
- [ ] Define canonical material parameter packing for rendering.
- [ ] Define CPU-side material representation vs. GPU packed representation.
- [ ] Plan material feature flags:
  - [ ] Opaque.
  - [ ] Masked.
  - [ ] Transparent.
  - [ ] Double-sided.
  - [ ] Emissive.
  - [ ] Special shading models.
- [ ] Plan material graph/node-system scope, or explicitly defer it.
- [ ] Plan shader permutation containment strategy.
- [ ] Plan texture binding model.
- [ ] Plan material debug visualization modes.

### C2. Transparency Architecture

- [ ] Plan the transparent rendering path.
- [ ] Decide whether transparency stays fully forward.
- [ ] Plan ordering/composition rules.
- [ ] Plan debug-view behavior for transparent objects.
- [ ] Plan interaction with selection outlines.

### C3. Lighting System Rewrite / Expansion

- [ ] Plan light data structures for forward+, deferred, and hybrid.
- [ ] Plan clustered/tiled light culling support.
- [ ] Plan shadow integration points.
- [ ] Plan emissive/material/light interaction.
- [ ] Plan debug views for lights and shading terms.

### C4. Visibility System Improvements

- [ ] Plan depth-prepass support.
- [ ] Plan a visibility buffer or material-classification path if desired later.
- [ ] Plan occlusion-culling integration.
- [ ] Plan GPU-driven pass compatibility with new graph stages.

### C5. Motion/Reprojection Support

- [ ] Plan velocity-buffer support.
- [ ] Plan history-resource ownership.
- [ ] Plan TAA integration points.
- [ ] Plan motion-blur integration points.
- [ ] Plan camera/object motion contracts.

### C6. Decals / SSAO / Screen-Space Effects

- [ ] Plan resource requirements for SSAO.
- [ ] Plan normal/depth usage for screen-space effects.
- [ ] Plan decal insertion points in the graph.
- [ ] Plan debug output for effect intermediates.

### C7. PostProcessPass Factoring

- [ ] Factor `PostProcessPass` into sub-pass classes (bloom, SMAA, tone mapping, histogram) to reduce single-class complexity.

### C9. Render Asset / Shader System Cleanup

- [ ] Plan shader registration refactor.
- [ ] Plan shader hot-reload boundaries by pass/stage.
- [ ] Plan permutation management.
- [ ] Plan shader feature-key derivation from material/frame recipe.
- [ ] Plan pipeline-cache invalidation strategy.

### C10. Scene Serialization Compatibility

- [ ] Ensure render settings serialize cleanly.
- [ ] Ensure frame-recipe-relevant settings are serializable where appropriate.
- [ ] Plan material serialization compatibility with the future rewrite.
- [ ] Plan debug/editor-only render state separation from scene state.

---

## 4. Planned Constraints — Design Now, Build Later

These are the explicit constraints agents must preserve during the refactor even when the underlying features are not being implemented yet.

- [ ] Leave room for a material-system rewrite.
- [ ] Leave room for transparency-path separation.
- [ ] Leave room for deferred and hybrid lighting.
- [ ] Leave room for motion vectors/history buffers.
- [ ] Leave room for clustered/tiled lighting.
- [ ] Leave room for effect passes like SSAO, decals, and bloom.
- [ ] Leave room for future GPU-driven visibility integration.

---

## 5. Sequencing & Definition of Done

### Sequencing Rules

- Finish P0 before starting P1 feature work unless the task is pure documentation, instrumentation, or test scaffolding that directly de-risks P0.
- Treat P2 as design pressure during P0, not as implementation scope creep.
- Keep selection/debug independent of the chosen lighting path.
- Do not allocate every buffer every frame unless required by the active `FrameRecipe`.
- Do not force full deferred shading everywhere yet.

### Cross-Cut Definition of Done

- [ ] Update `README.md` for each merged refactor milestone when user-facing architecture or workflow changes.
- [ ] Remove superseded code paths immediately (no compatibility clutter beyond staged migration windows).
- [ ] Add at least one integration test per milestone and wire it into the existing test targets.
- [ ] Add or update migration notes when render contracts change.

---

## 6. Codebase Cleanup & Refactor (Audit Findings)

Identified via full codebase sweep (March 2026). Grouped by priority.

No active items. Previous findings (D3 Core.Memory token pattern, D9 SpatialDebugController boolean flags, D9 god-object decomposition) have been resolved — see git history for details.

---

## 7. Pattern Adoption & Contract Compliance (Audit March 2026)

Sourced from `plan.md` pattern review and full codebase sweep.

### E1. Command Pattern for Undo/Redo (P1)

ROADMAP Phase 2 lists "Undo/redo stack" but no implementation exists. Add `Core.Command.cppm`:

- [ ] `Core::Command` base with `Execute()` / `Undo()`, returning `std::expected`.
- [ ] `Core::CompositeCommand` for transaction grouping (multi-entity transform).
- [ ] `Core::CommandHistory` with fixed-size ring buffer.
- [ ] Integrate with `TransformGizmo`: each drag produces a `TransformCommand` capturing before/after state.
- [ ] Integrate with property panel mutations (material, point size, line width).
- [ ] Integrate with entity create/delete (wrapped as commands).
- [ ] Integrate with geometry operator application (mesh snapshot before operator, undo reverts).

### E2. Geometry Operator Contract Compliance (P2)

Six operators deviate from the standard Params/Result/`std::optional<Result>` contract.

- [ ] `Geometry.Smoothing.cppm`: `UniformLaplacian()`, `CotanLaplacian()`, `Taubin()` return `void`. Add `SmoothingResult` struct with iteration count and convergence diagnostics; return `std::optional<SmoothingResult>`.
- [ ] `Geometry.Curvature.cppm`: `ComputeMeanCurvature()`, `ComputeGaussianCurvature()` return raw `std::vector<double>`. Add `CurvatureParams` and wrap in `std::optional<Result>` with vertex count diagnostics.
- [ ] `Geometry.MeshRepair.cppm`: `FindBoundaryLoops()` returns raw `std::vector<BoundaryLoop>`. Wrap in `std::optional` for degenerate-input consistency.

### E3. `std::expected` Monadic Chaining Adoption (P3)

Three files have 3+ sequential fallible stages that are strong candidates for `.and_then()` / `.transform()` chaining (per CLAUDE.md C++23 adoption policy):

- [ ] `Core.IOBackend.cpp`: `FileIOBackend::Read()` — 6 sequential `if (!...) return std::unexpected(ErrorCode::...)` checks. Classic monadic chain, single error type.
- [ ] `Graphics.IORegistry.cpp`: `Export()` — 3 stages (find exporter → export bytes → write to backend) with error mapping at module boundary.
- [ ] `Graphics.Importers.PLY.cpp`: Nested scalar/face parsing sub-pipelines with repetitive `if (!result) return std::unexpected(AssetError::DecodeFailed)`.

### E4. C++23 `std::views::enumerate` Adoption (P3)

109+ manual `for (size_t i = 0; i < N; ++i)` index loops across 46 files could use `std::views::enumerate`. Adopt opportunistically when touching these files — no dedicated churn PR.

Top targets (by loop count):
- [ ] `Geometry.Octree.cpp` (13 loops) — spatial query hot paths.
- [ ] `Graphics.RenderGraph.cpp` (8 loops) — frame graph compilation.
- [ ] `Graphics.Importers.PLY.cpp` (5 loops) — vertex/face element parsing.
- [ ] `Graphics.Passes.Surface.cpp` (4 loops) — frustum culling.
- [ ] `Graphics.Passes.Point.cpp` (4 loops) — point attribute buffers.
