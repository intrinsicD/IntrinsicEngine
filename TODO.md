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

### B5. Post-Processing Test Coverage

- [ ] Add descriptor layout / pipeline build success tests for bloom, SMAA, and tone mapping passes.

### B6. Render Graph & Frame Construction

- [ ] Add tests for `CanonicalResources` registration and recipe-driven allocation.

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

### D1. Halfedge Loop Safety Limits (Critical — 12 violations)

CLAUDE.md mandates `if (++safety > N) break;` on all `CWRotatedHalfedge` loops. 12 loops are missing this guard, risking infinite loops on corrupted topology.

- [ ] `Geometry.Smoothing.cpp:57–64` — `UniformLaplacianPass()` vertex ring traversal.
- [ ] `Geometry.DEC.cpp:210–213` — `BuildExteriorDerivative0()` face halfedge iteration.
- [ ] `Geometry.DEC.cpp:483–486` — `BuildHodgeStar1()` edge cotan weight computation.
- [ ] `Geometry.DEC.cpp:536–539` — `BuildLaplacian()` weak Laplacian assembly.
- [ ] `Geometry.Graph.cpp:360–364` — Graph edge accumulation (initialization).
- [ ] `Geometry.Graph.cpp:1318–1322` — Hierarchical graph layout computation.
- [ ] `Geometry.HalfedgeMesh.cpp:242–246` — `IsManifold()` ring check.
- [ ] `Geometry.HalfedgeMesh.cpp:266–270` — `Valence()` computation.
- [ ] `Geometry.HalfedgeMesh.cpp:553–557` — Internal ring iteration utility.
- [ ] `Geometry.HalfedgeMesh.cpp:603–607` — Face halfedge cycle iteration.
- [ ] `Geometry.Subdivision.cpp:75–83` — Loop subdivision boundary vertex rule.
- [ ] `Geometry.Subdivision.cpp:102–108` — Loop subdivision interior vertex averaging.

### D2. Standardize CatmullClark Safety Bounds (Medium)

- [ ] Replace hardcoded `1000` safety limits in `Geometry.CatmullClark.cpp` (lines 53, 128, 165, 257) with dynamic `std::max<std::size_t>(mesh.HalfedgesSize(), 1000u)` to match `Curvature.cpp` and `Simplification.cpp` patterns.

### D3. Core Module Cleanup (Medium)

- [ ] Replace raw `new`/`delete` of `ArenaLifetimeToken` in `Core.Memory.cpp` (lines 48, 84, 92, 110) with `std::unique_ptr`.
- [ ] Add safety iteration limits to unbounded linked-list traversals in `Core.Tasks.cpp` — `ReleaseWaitToken` (lines 735–744) and `UnparkReady` (lines 850–860).
- [ ] Fix malformed comment in `Core.Tasks.cpp` line 65 (`/ 3)` → `// 3)`).
- [ ] Fix double space in `#include` in `Core.Hash.cppm` line 5.

### D4. Sandbox App Robustness (Medium)

- [ ] Replace dangling raw pointers to `GeometryCollisionData` in `main.cpp` (lines 198, 217, 225, 243, 248, 251) with `entt::entity` handles validated via `registry.try_get<>()` before use.
- [ ] Add bounds check to fixed-size octree traversal stack in `main.cpp` line 697 (currently `std::array<StackItem, 512>` with no overflow guard).
- [ ] Move `static char[]` ImGui buffers in `Runtime.EditorUI.cpp` (lines 39, 203, 241, 269) to a persistent state struct to avoid stale state across panel hide/show cycles.

### D5. Deprecated API Removal (Low)

- [ ] Remove deprecated `RHI::CommandUtils::ExecuteImmediate()` (RHI.CommandUtils.cppm:93) — callers should use `BeginSingleTimeCommands()`/`EndSingleTimeCommands()`.
- [ ] Remove deprecated `RHI::VulkanDevice::IncrementFrame()` (RHI.Device.cppm:76) — callers should use `IncrementGlobalFrame()` and `AdvanceFrameIndex()`.

### D6. Dead / Commented-Out Code (Low)

- [ ] Resolve commented-out normal map texture slot in `Graphics.MaterialSystem.cpp:129` (`// if (slotType == 1) data->NormalID = bindlessID;`) — either implement normal map support or document the omission.
- [ ] Clarify or remove dead callback invocation in `Core.Filesystem.cpp:283` (`//entry.Callback(entry.Path.string());`).

### D7. Test Coverage Gaps (Low)

- [ ] Expand `Test_Boolean.cpp` (currently 3 tests / 64 lines): add `Operation::Difference` tests, degenerate input tests (coplanar faces, coincident vertices, inverted winding), and edge-contact tests.
- [ ] Add `AxisRotator` system unit tests (rotation correctness, dirty tag emission).
- [ ] Improve `Test_EditorUI.cpp` beyond symbolic function-pointer check — add at least panel registration and state validation tests.

### D8. Build & Tooling Hardening (Low)

- [ ] Harden `tools/check_perf_regression.sh` JSON extraction (lines 58–66): regex fails on standard `"field": value` JSON format; add numeric validation so empty extraction doesn't silently report PASS.

### D9. Sandbox Architecture (Opportunistic)

- [ ] Extract debug visualization subsystems (Octree, KDTree, BVH, ConvexHull, Bounds, Contacts) from `SandboxApp` god object (~80 members, 2700+ lines) into dedicated classes.
- [ ] Replace boolean debug-visualization flags with ECS presence/absence pattern per CLAUDE.md convention.
- [ ] Replace hardcoded buffer sizes (512, 128, 256) with named constants documenting their rationale.
