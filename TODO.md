# IntrinsicEngine — Architecture TODOs (Living Document)

This document tracks the **active rendering-architecture backlog** for IntrinsicEngine.

**Policy:** completed/refactored work does **not** stay here. We rely on Git history for the past and keep this file focused on what remains.

---

## 0. Scope & Success Criteria

**Current focus:** finish the resource-driven rendering refactor and expand lighting paths without painting the engine into a corner for hybrid, transparency, or material-system work.

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
- `PATTERNS.md` — reusable patterns catalog with canonical examples and usage guidance.

---

## P0 — Critical Refactor

*(All P0 items completed — see git history for implementation details.)*

## 2. Next (P1) — Near-Term Follow-Up After the Refactor Lands

These are not required to finish the first wave, but they should begin soon after P0 is stable.

### B2. Hybrid Renderer Support

- [ ] Support forward + deferred coexistence.
- [ ] Define which primitives/materials are deferred-capable.
- [ ] Keep transparent/special/debug rendering in the forward path.
- [ ] Define composition rules between paths.

### B3. Engine Architecture Review Follow-Up (Boundary + Coupling + Migration)

#### B3.1 Current Architecture Map (baseline and keep current)

- [ ] Document the current subsystem boundaries and ownership map (`Engine`, `GraphicsBackend`, `RenderOrchestrator`, `SceneManager`, `AssetPipeline`).
- [ ] Document explicit dependency directions between `Core`, `Runtime`, `Graphics`, `RHI`, `Geometry`, `ECS`, and `Interface`.
- [ ] Publish a one-page runtime lifecycle map (startup, per-frame, shutdown) with the current execution order.

#### B3.2 Coupling Hotspots (reduce first)

- [ ] Reduce `Engine` orchestration coupling by introducing lane-level coordinators (simulation/render/streaming) while preserving behavior.
- [ ] Reduce repetitive system registration glue in `Engine::Run()` via typed registration bundles.
- [ ] Remove global/file-static GPU hook state from `SceneManager` and replace with instance-scoped callback context.

#### B3.3 Mixed Concerns + Unstable Interfaces

- [ ] Separate drag-drop ingest orchestration from `Engine` into a dedicated asset ingest service.
- [ ] Separate frame-loop policy from subsystem wiring (registration/configuration vs. per-frame execution).
- [ ] Replace string-based feature toggle callsites in hot orchestration paths with typed feature descriptors where practical.

#### B3.4 Barriers to Testing + Evolution

- [ ] Add seam-friendly interfaces for `Engine` dependencies to allow isolated tests without full Vulkan/runtime boot.
- [ ] Add tests that lock frame-order contracts (fixed-step + variable-step + dispatcher + render handoff).
- [ ] Add tests that lock asset-streaming completion semantics (queued -> uploaded -> finalized).

#### B3.5 Hidden Architectural Duplication

- [ ] Consolidate repeated geometry sync registration patterns (graph/mesh-view/point-cloud/GPU-scene) behind shared helpers.
- [ ] Consolidate repeated GPU-slot reclaim lifecycle logic into a single policy utility with shared tests.
- [ ] Audit duplicate upload/lifecycle code paths between retained geometry systems and document consolidation plan.

#### B3.6 Redesign Options (decision package)

##### Option O1 — Minimal Change

- [ ] Write an ADR for O1 that explicitly captures:
  - [ ] Benefits
  - [ ] Drawbacks
  - [ ] Migration cost
  - [ ] Regression risk
  - [ ] Performance impact
  - [ ] Testability impact
  - [ ] Future extensibility impact

##### Option O2 — Pragmatic Medium Refactor

- [ ] Write an ADR for O2 that explicitly captures:
  - [ ] Benefits
  - [ ] Drawbacks
  - [ ] Migration cost
  - [ ] Regression risk
  - [ ] Performance impact
  - [ ] Testability impact
  - [ ] Future extensibility impact

##### Option O3 — Ideal Target Architecture

- [ ] Write an ADR for O3 that explicitly captures:
  - [ ] Benefits
  - [ ] Drawbacks
  - [ ] Migration cost
  - [ ] Regression risk
  - [ ] Performance impact
  - [ ] Testability impact
  - [ ] Future extensibility impact

#### B3.7 Recommended Path (default = O2) + Migration Plan

- [ ] Ratify O2 as the default path unless new benchmark/test evidence disproves it.
- [ ] Execute phased migration with safe checkpoints:
  - [ ] Phase 0: Baseline lock (telemetry/order/contract snapshots).
    - [ ] Safe checkpoint: no behavioral diff vs baseline in frame order + render contracts.
  - [ ] Phase 1: Extract simulation/render/streaming lanes (no behavior change).
    - [ ] Safe checkpoint: same pass/system order and same frame outputs as baseline.
  - [ ] Phase 2: Replace global hook state with instance-scoped callbacks + typed system bundles.
    - [ ] Safe checkpoint: lifecycle/resource reclaim tests unchanged.
  - [ ] Phase 3: Move drag-drop + async load orchestration into streaming service state machine.
    - [ ] Safe checkpoint: asset ingest completion/integrity metrics unchanged.
  - [ ] Phase 4: Harden typed contracts for hybrid/deferred and post-process factoring.
    - [ ] Safe checkpoint: render-graph validation and pass-contract suites remain green.

- [ ] Define rollback strategy before each phase starts:
  - [ ] Maintain a feature flag to route back to legacy orchestration path.
  - [ ] Keep adapter shims for one migration window (then delete).
  - [ ] Require pass/fail gates (tests + telemetry budgets) to permit cutover.

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

### C8. Render Asset / Shader System Cleanup

- [ ] Plan shader registration refactor.
- [ ] Plan shader hot-reload boundaries by pass/stage.
- [ ] Plan permutation management.
- [ ] Plan shader feature-key derivation from material/frame recipe.
- [ ] Plan pipeline-cache invalidation strategy.

### C9. Scene Serialization Compatibility

- [ ] Ensure render settings serialize cleanly.
- [ ] Ensure frame-recipe-relevant settings are serializable where appropriate.
- [ ] Plan material serialization compatibility with the future rewrite.
- [ ] Plan debug/editor-only render state separation from scene state.

---

## 4. Planned Constraints — Design Now, Build Later

These are the explicit constraints agents must preserve during the refactor even when the underlying features are not being implemented yet.

- [ ] Leave room for a material-system rewrite.
- [ ] Leave room for transparency-path separation.
- [ ] Leave room for hybrid lighting.
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
