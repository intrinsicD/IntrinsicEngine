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

## 2. Now (P0) — Bugs & Correctness Issues

### A1. SMAA Lookup Textures

- [ ] Validate custom area/search texture generators in `Graphics.SMAALookupTextures.hpp` against the Jimenez et al. reference precomputed tables, or replace with canonical binary data from the original SMAA distribution.

### A2. Bloom Downsample Dead Code

- [ ] Fix dead code in `assets/shaders/post_bloom_downsample.frag` (~lines 119-123): the first `result` computation (Karis average threshold path) is immediately overwritten by the second.

### A3. Contact Manifold Retained Overlay

- [ ] Fix silent failure in contact manifold retained overlay rendering — verify the DebugDraw overlay path correctly displays manifold data when toggled on.

---

## 3. Next (P1) — Near-Term Follow-Up After the Refactor Lands

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

### B4. Post-Processing Correctness & Cleanup

- [ ] Remove spurious `VK_ACCESS_2_MEMORY_WRITE_BIT` from `builder.Read()` calls in `Graphics.Passes.PostProcess.cpp` — reads should not declare write access.
- [ ] Replace string-based resource lookups in PostCompile (bloom upsample binding 1, SMAA edge/weight bindings) with cached handles.
- [ ] Remove dead write to `m_LastSMAAEdgesHandle` (assigned in edge pass setup, overwritten in blend pass setup, never used).
- [ ] Pass integer `width`/`height` directly to histogram compute shader instead of reconstructing from inverse floats.

### B5. Post-Processing Test Coverage

- [ ] Add contract tests for post-processing push constant struct sizes (`static_assert`).
- [ ] Add descriptor layout / pipeline build success tests for bloom, SMAA, and tone mapping passes.

### B6. Render Graph & Frame Construction

- [ ] Add tests for `CanonicalResources` registration and recipe-driven allocation.
- [ ] Add tests for `ValidateCompiledGraph()` with the new post-processing passes included.

### B7. Application Event Bus (`entt::dispatcher`)

- [ ] Add `entt::dispatcher` to `ECS::Scene` with `GetDispatcher()` accessors.
- [ ] Create `ECS.Components.Events.cppm` with initial event struct definitions.
- [ ] Add `dispatcher.update()` drain point in `Engine::Run()` main loop.
- [ ] Fire `SelectionChanged` / `HoverChanged` from `ApplySelection` / `ApplyHover`.
- [ ] Replace `GetSelectedEntity()` polling in Sandbox panels with `SelectionChanged` sink.
- [ ] Fire `GpuPickCompleted` from readback completion, replace polling in `SelectionModule`.
- [ ] Fire `EntitySpawned` from `SceneManager::SpawnModel()` and `LoadDroppedAsset`.
- [ ] Fire `GeometryModified` from centralized geometry operator helper.
- [ ] Add contract tests for event delivery.
- [ ] Document event communication policy in `CLAUDE.md`.

---

## 4. Later (P2) — Planned Downstream Work

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
- [ ] Use `glm::vec3` for color grading lift/gamma/gain instead of individual `float` fields.

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

## 5. Planned Constraints — Design Now, Build Later

These are the explicit constraints agents must preserve during the refactor even when the underlying features are not being implemented yet.

- [ ] Leave room for a material-system rewrite.
- [ ] Leave room for transparency-path separation.
- [ ] Leave room for deferred and hybrid lighting.
- [ ] Leave room for motion vectors/history buffers.
- [ ] Leave room for clustered/tiled lighting.
- [ ] Leave room for effect passes like SSAO, decals, and bloom.
- [ ] Leave room for future GPU-driven visibility integration.

---

## 6. Sequencing & Definition of Done

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
