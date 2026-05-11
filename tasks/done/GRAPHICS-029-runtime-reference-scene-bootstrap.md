# GRAPHICS-029 — Reference scene bootstrap and minimal renderable extraction (planning)

## Goal
Lock down the runtime-owned, opt-in reference scene bootstrap contract that produces at least one deterministic renderable candidate observable by `Runtime.RenderExtraction` when `ExtrinsicSandbox` runs, with all placement, modularity, extensibility, and performance decisions recorded before any implementation. This is a planning/owner task; no engine behavior changes here.

## Non-goals
- No implementation, no new modules added to the build, no new ECS components, no shader/material/pipeline changes in this slice.
- No content authored inside `src/app/Sandbox/*`; sandbox stays policy-light per the app/runtime boundary.
- No GPU upload, no `GpuWorld::SetInstanceGeometry()` binding (those are GRAPHICS-030 territory).
- No asset-file loading; asset-backed candidates are GRAPHICS-034 territory.
- No GPU-typed components inside `src/ecs/Components/` (per AGENTS.md §2 and GRAPHICS-028).
- No renderer pass body or device backend changes; null device remains the supported execution path until GRAPHICS-032 / GRAPHICS-033 land.

## Context
- Owner layer: `runtime`. Source-tree home is a runtime sample/bootstrap seam, composed by `Runtime.Engine` when reference config opts in. Final module name and namespace are decided in this task.
- `src/app/Sandbox/Sandbox.cppm` lifecycle hooks intentionally do not mutate engine behavior; `src/app/Sandbox/main.cpp` already obtains configuration through `Extrinsic::Runtime::CreateReferenceEngineConfig()`.
- `src/runtime/Runtime.RenderExtraction.cppm` qualifies a renderable by requiring `ECS::Components::Transform::WorldMatrix` plus at least one render hint from `Graphics::Components::RenderSurface`, `RenderLines`, or `RenderPoints`. With no ECS content, extraction sees zero candidates today.
- The 2026-05-08 review (`docs/reviews/2026-05-08-sandbox-geometry-rendering-gap-analysis.md`, sections "Exact missing pieces / 1" and "minimal milestone plan / 1") records this as the first gap.
- `docs/architecture/graphics.md` and the GRAPHICS-028 done task already require any residency-bridge consumer to work from the runtime cache keyed by stable entity ID.
- `HARDEN-060` retired the promoted `ECS::Scene::CreateDefault(Registry&, std::string_view)` API used by this bootstrap; `HARDEN-061` retired the promoted `TransformHierarchy` system that owns world-matrix recompute; both are reusable foundations for the bootstrap entity.

## Recorded design decisions

Each decision below is locked for downstream implementation children. Trade-offs are recorded so reviewers can see what was rejected and why.

1. **Module placement and naming.**
   - Decision: a new module `Extrinsic.Runtime.ReferenceScene` at `src/runtime/Runtime.ReferenceScene.cppm`, namespace `Extrinsic::Runtime`. Implementation lives in `src/runtime/Runtime.ReferenceScene.cpp` when behavior is added (GRAPHICS-029-Impl-A).
   - Rejected alternatives: (i) placing the bootstrap inside `Extrinsic.Runtime.Engine` would tangle composition-root wiring with sample content and force `Engine` to depend on graphics-render-hint component imports for content authoring, not just composition; (ii) a sandbox-only module would violate the app/runtime boundary recorded in the 2026-05-08 review (sandbox stays policy-light and consumes `CreateReferenceEngineConfig()` only); (iii) a graphics-side fixture would violate AGENTS.md §2 because it would require graphics to write into the live ECS registry, which `RenderExtraction` is the only allowed party to query.

2. **Bootstrap-seam shape.**
   - Decision: option (b) — a runtime-owned `IReferenceSceneProvider` interface with one default implementation (`TriangleProvider`) registered by `CreateReferenceEngineConfig()` into a `ReferenceSceneRegistry` consumed by `Engine::Initialize()` after subsystem wiring and before the first frame.
   - Trade-off: option (a) (a single config flag read by `Runtime.Engine`) is simplest but does not compose when a second sample scene (cube, hierarchy demo) is added without forking the module. Option (c) (a callback installed via reference engine config) gives tests maximum flexibility but defeats the goal of declarative reference content and creates a mutable indirect dependency from `Core.Config` into entity composition. Option (b) keeps the registration declarative, supports trivially adding sibling providers behind a `ReferenceSceneSelector` enum, and is mockable in tests by registering a test-only provider in a contract test fixture.

3. **Renderable composition.**
   - Decision: the candidate carries exactly:
     - `ECS::Components::MetaData{ EntityName = "ReferenceTriangle" }` (HARDEN-060 default-component contract);
     - `ECS::Components::Transform::Component{}` (identity TRS) and `Transform::WorldMatrix{}` (identity matrix), both seeded by `ECS::Scene::CreateDefault`;
     - `ECS::Components::Hierarchy::Component{}` (no parent/children), again from `CreateDefault`;
     - exactly one render hint — `Graphics::Components::RenderSurface{ Domain = SourceDomain::Vertex }`. `RenderLines` / `RenderPoints` are explicitly excluded from the first milestone.
     - the procedural-source link is the CPU-only `ECS::Components::ProceduralGeometryRef{ Kind = ProceduralGeometryKind::Triangle }` once GRAPHICS-030-Impl-A lands the type. Until that child lands, the bootstrap entity carries the render hint only and `RenderExtraction` reports it as a candidate without a geometry handle (matches the milestone-plan "CPU/null contract milestone" in the 2026-05-08 review).
   - The optional `Renderable {}` core-only marker recorded as discretionary by GRAPHICS-028 is **deferred**. `RenderSurface` plus the GRAPHICS-030 procedural reference already filter the candidate uniquely; introducing another marker now would add noise to ECS without test value.
   - **Forbidden on the entity (locked):** any GPU-typed value type — `Graphics::Components::GpuSceneSlot`, `GpuInstanceHandle`, `RHI::BufferHandle`, `RHI::TextureHandle`, bindless indices, material lease handles, or any field that implies a graphics resource. Per AGENTS.md §2 and GRAPHICS-028, residency state lives in the runtime extraction sidecar, never on the entity.

4. **Camera / view authorship.**
   - Decision: option (b) — `Runtime.Engine` populates `RenderFrameInput::Camera` directly from a `ReferenceCameraView { Graphics::CameraViewInput }` value seeded by the active reference scene provider. Concretely, `IReferenceSceneProvider::Populate(scene)` returns both the entity handles it created and a `std::optional<CameraViewInput>` that `Engine::BuildRenderFrameInput()` substitutes into the existing `RenderFrameInput::Camera` field when the reference flag is enabled and no GRAPHICS-017Q `CameraControllers` is yet wired.
   - Why not option (a) (a camera ECS component): GRAPHICS-017Q already records that runtime camera controllers will live under the planned umbrella `Extrinsic.Runtime.CameraControllers`. Introducing a competing camera ECS component now would force a migration once that umbrella opens. Option (b) is forward-compatible: when `CameraControllers` lands, the reference scene's `CameraViewInput` becomes the *seed* the controller starts from, with no contract change.
   - Default values for the triangle provider: `Position = (0, 0, 3)`, `Forward = (0, 0, -1)`, `Up = (0, 1, 0)`, perspective with 45° vertical FoV, aspect derived from `RenderFrameInput::Viewport`, near 0.1, far 100. `CameraViewInput::Valid` is set to `true`.

5. **Light / unlit policy.**
   - Decision: **no light** in the first milestone. The reference triangle relies entirely on the GRAPHICS-031 default debug surface material, which is unlit by contract. Lit-default policy is **deferred** until (i) GRAPHICS-031 lands a default debug material and (ii) a follow-up provider opts into a lit material.
   - Rationale: an unlit-default keeps the milestone independent of GRAPHICS-009/039 and keeps the reference scene runnable through the null renderer for CPU/null tests.

6. **Stable ID and naming policy.**
   - Decision: bootstrap acquires the entity by calling `ECS::Scene::CreateDefault(scene, "ReferenceTriangle")` (HARDEN-060 promoted bootstrap API). The returned `EntityHandle` (an `entt::entity`) is **not** stable across runs — entt's allocator does not promise determinism across registries. Determinism for tests is achieved at the **content layer**, not the entity-ID layer:
     - The provider returns a `std::span<ReferenceSceneEntity>` (or equivalent owning vector) whose indices are deterministic in registration order.
     - Contract tests query by `MetaData::EntityName == "ReferenceTriangle"` for assertion. Two consecutive bootstraps in the same test produce the same name, the same component set, and the same provider-returned index regardless of `entt::entity` value.
     - The debug name surfaced into `GpuSceneSlot` diagnostics (when GRAPHICS-030 wires geometry residency) is the same `MetaData::EntityName` string.
   - Stable-ID consumers (`StableEntityId(entity) = static_cast<std::uint32_t>(entity)` in `Runtime.RenderExtraction`) read the runtime-assigned ID; tests do not assert on its value.

7. **Lifetime and reset semantics.**
   - Decision: the bootstrap runs **once per `Engine::Initialize()`**, immediately after `m_Scene` is constructed and after `m_Renderer` is wired but before the first `Engine::RunFrame()`. Teardown runs in `Engine::Shutdown()` before scene tear-down via `IReferenceSceneProvider::Teardown(scene, providerEntities)`, which destroys the entities the provider created.
   - Idempotency: the bootstrap is **not** idempotent within a single `Engine` lifetime — a `bool m_ReferenceSceneInstalled` guard rejects double-install. Editor/runtime hot-reload paths must call `Engine::Shutdown()` before re-`Initialize()`; this matches existing subsystem teardown order. Per-frame reset is explicitly rejected (would burn allocations and break extraction sidecars across frames).

8. **Extensibility surface.**
   - Decision: additional sample scenes register additional `IReferenceSceneProvider` implementations through `ReferenceSceneRegistry::Register(selector, provider)`. The reference engine config carries a single-selector field `EngineConfig::ReferenceScene::Selector = ReferenceSceneSelector::Triangle`; the registry resolves the selector at `Engine::Initialize()` time and invokes one provider.
   - Initial provider set (this slice + GRAPHICS-029-Impl-A/B): `Triangle` only.
   - Next provider candidates (enumerated, **not** opened): `Cube` (single static asset, gates on GRAPHICS-034 asset-backed mesh residency), `MultiPrimitive` (cluster of primitive instances exercising GRAPHICS-028's instancing policy, gates on GRAPHICS-030 procedural geometry residency landing the cube/quad packer), `HierarchyDemo` (parent/child transform composition exercising HARDEN-061's `TransformHierarchy` propagation). These belong to `GRAPHICS-029-Impl-C` (optional) and are gated as recorded.
   - Multiple-provider composition (e.g., a triangle plus a hierarchy demo) is **out of scope** for this task; the registry resolves exactly one selector per init. Composition is a forward-extension question, not a milestone-zero requirement.

9. **Configuration plumbing.**
   - Decision: the configuration field lives on `EngineConfig`, **not** `RenderConfig`. `RenderConfig` governs backend/validation/vsync/frames-in-flight; reference content is engine-level. Concrete shape:
     - `Extrinsic::Core::Config::EngineConfig::ReferenceScene { bool Enabled = false; ReferenceSceneSelector Selector = ReferenceSceneSelector::Triangle; }`.
     - `ReferenceSceneSelector` enum lives next to `ReferenceScene` in `Core.Config.Engine` (or a new `Core.Config.ReferenceScene` partition; the implementation child decides whether to introduce a partition).
     - `CreateReferenceEngineConfig()` flips `ReferenceScene.Enabled = true` and `ReferenceScene.Selector = ReferenceSceneSelector::Triangle`.
     - `Sandbox::main.cpp` does **not** flip the flag — it consumes `CreateReferenceEngineConfig()` directly per the existing pattern, so sandbox stays policy-light.
   - **Default-off rationale:** existing CPU/null tests construct `EngineConfig{}` directly. With `Enabled = false` they observe the current null/empty-scene behavior and do not regress. Tests that exercise the bootstrap construct `CreateReferenceEngineConfig()` or set the flag explicitly.

10. **Performance budget.**
    - Decision: bootstrap is a once-per-init operation; per-frame steady-state cost must remain proportional to the renderable count, not to provider count.
    - Provider entity-count upper bound for `TriangleProvider`: **exactly one entity** with five components (`MetaData`, `Transform::Component`, `Transform::WorldMatrix`, `Hierarchy::Component`, `RenderSurface`) plus one CPU-only `ProceduralGeometryRef` once GRAPHICS-030-Impl-A lands. Total ECS allocation cost is constant.
    - Per-tick `RenderExtraction` overhead with the reference scene enabled: O(1) — one renderable candidate, one sidecar allocated on the first tick, reused thereafter; no per-frame allocation/copy work on the steady-state path.
    - Forward extensibility constraint: future multi-entity providers must remain O(N) in entity count (no quadratic dependency-walk paths). `MultiPrimitive` is bounded by configured cluster size at construction time.

11. **Layering audit.**
    - Decision: `src/runtime/Runtime.ReferenceScene.cppm` imports only:
      - `Extrinsic.ECS.Scene.Registry`,
      - `Extrinsic.ECS.Scene.Bootstrap` (`CreateDefault`),
      - `Extrinsic.ECS.Component.MetaData`,
      - `Extrinsic.ECS.Component.Transform`,
      - `Extrinsic.ECS.Component.Transform.WorldMatrix`,
      - `Extrinsic.ECS.Component.Hierarchy`,
      - `Extrinsic.Graphics.Component.RenderGeometry` (for `Graphics::Components::RenderSurface`; this is a CPU-only render-hint value type already imported by `Runtime.RenderExtraction`, so no new dependency edge),
      - `Extrinsic.Graphics.CameraSnapshots` (for `CameraViewInput`; already imported by `Runtime.Engine` via `Graphics.RenderFrameInput`),
      - `Extrinsic.Core.Config.Engine`.
    - It does **not** import `Extrinsic.Graphics.GpuWorld`, `Extrinsic.Graphics.Renderer`, `Extrinsic.Graphics.GpuAssetCache`, `Extrinsic.Graphics.MaterialSystem`, any `Extrinsic.RHI.*`, any `Extrinsic.Asset.*`, or any `Extrinsic.Platform.*`.
    - The procedural-geometry reference (per GRAPHICS-030 decision 7a) lives under `src/ecs/Components/ECS.Component.ProceduralGeometryRef.cppm` (`core`-only, no graphics imports), so importing it from runtime introduces no new edge.
    - Promoted layering invariants therefore hold: `runtime → core, ecs, graphics-value-types`; no new graphics or asset traffic; no live-ECS leak into graphics.

## Required changes
- [x] ✅ Recorded the eleven design decisions above with explicit answers and rejected-alternative rationale.
- [x] ✅ Cross-linked the decisions into `docs/architecture/graphics.md` (new "Reference scene bootstrap" subsection adjacent to the residency-bridge section) and `src/runtime/README.md` (planned-module entry).
- [x] ✅ Identified split points for the implementation follow-up children (do **not** open them in this slice):
  - [x] **GRAPHICS-029-Impl-A** — author `Extrinsic.Runtime.ReferenceScene` skeleton: `IReferenceSceneProvider` interface, `ReferenceSceneRegistry`, `EngineConfig::ReferenceScene::{Enabled, Selector}` field with `ReferenceSceneSelector` enum, default `Selector = Triangle`, `CreateReferenceEngineConfig()` flips `Enabled = true`, `Engine::Initialize()` invokes the selected provider after scene construction. No provider implementation lands here; the registry resolves to a no-op default. Tests: `contract;runtime` assertion that with `Enabled = false`, `RenderExtractionCache::ExtractAndSubmit` reports zero renderable candidates.
  - [x] **GRAPHICS-029-Impl-B** — implement `TriangleProvider`: creates the entity per Decision 3, returns the entity handle list, returns `CameraViewInput` per Decision 4. Wire `Engine::BuildRenderFrameInput()` to substitute the seeded camera. Tests: `contract;runtime` assertions that with `Enabled = true`, extraction reports exactly one renderable candidate carrying `RenderSurface`, `Transform::WorldMatrix`, `MetaData::EntityName == "ReferenceTriangle"`, and (once GRAPHICS-030-Impl-A lands) `ProceduralGeometryRef::Kind == Triangle`. Extraction stats assert `CandidateRenderableCount == 1` and `AllocatedInstanceCount == 1`. Tests run against the null renderer; no GPU coverage.
  - [x] **GRAPHICS-029-Impl-C** *(optional, gated)* — open additional provider candidates (`Cube`, `MultiPrimitive`, `HierarchyDemo`). `Cube` gates on GRAPHICS-034 asset-backed mesh residency. `MultiPrimitive` gates on GRAPHICS-030 procedural-residency cube/quad packer. `HierarchyDemo` is unblocked once Impl-B lands. Each provider gets its own contract test asserting the candidate count and component composition.
- [x] ✅ Cross-linked decisions with GRAPHICS-016 (extraction handoff), GRAPHICS-017Q (camera ownership forward-compat), GRAPHICS-028 (residency bridge planning), GRAPHICS-030 (procedural geometry reference type), HARDEN-060 (`CreateDefault` bootstrap), HARDEN-061 (`TransformHierarchy` system), and the 2026-05-08 review.

## Tests
- [x] Planning slice (this task): task-policy, doc-link, and layering validators only — no engine behavior or test-source changes land here.
- [x] Future implementation children (GRAPHICS-029-Impl-A/B) must add `contract;runtime` tests asserting:
  - [x] With `EngineConfig::ReferenceScene::Enabled = false`, extraction reports zero renderable candidates (regression guard for sandbox staying policy-light).
  - [x] With `Enabled = true` and `Selector = Triangle`, extraction reports exactly one renderable candidate with the expected components and `MetaData::EntityName == "ReferenceTriangle"`.
  - [x] The provider's reported entity span has length 1 and the same `MetaData::EntityName` across two consecutive bootstraps in the same test (deterministic content even though `entt::entity` values differ).
  - [x] Double-install rejection: a second `Engine::Initialize()` without `Engine::Shutdown()` is rejected via the `m_ReferenceSceneInstalled` guard.
- [x] GPU coverage stays opt-in `gpu;vulkan` and is **not** introduced by GRAPHICS-029-Impl-A/B; visible-pass coverage belongs to GRAPHICS-032.

## Docs
- [x] Update `docs/architecture/graphics.md` with the bootstrap-seam decision and cross-link to this task.
- [x] Update `src/runtime/README.md` Public-module-surface table with a planned-module row for `Extrinsic.Runtime.ReferenceScene`.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` `app` row to reference GRAPHICS-029 as the planning gate for promoted reference-content authoring.
- [x] Update `tasks/backlog/rendering/README.md` and `tasks/backlog/README.md` to reflect that the planning slice is complete and the implementation children are gated as recorded.

## Acceptance criteria
- [x] All eleven design decisions above have explicit recorded answers in the task body before any implementation child is opened. ✅
- [x] Implementation follow-up children (Impl-A/B/C) are identified with scope, dependency gates, and test expectations but not opened. ✅
- [x] Architecture and source-tree docs reference the bootstrap-seam decision; layering invariants are unchanged. ✅
- [x] No engine behavior, no new modules in the build, and no test changes land in this slice. ✅

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No implementation, no new modules added to `CMakeLists.txt`, no new ECS components, no new graphics or runtime behavior in this slice.
- No content authored under `src/app/Sandbox/*`.
- No GPU-typed component additions to `src/ecs/Components/`.
- No mixing of mechanical file moves with semantic refactors.
- No premature opening of implementation child tasks before this planning slice is approved.

## Completion
- Completed: 2026-05-09.
- Commit reference: planning-only slice on branch `claude/setup-agentic-workflow-KzWE1`; no code/test/CMake changes landed.
- Notes:
  - All eleven design decisions are mirrored in `docs/architecture/graphics.md` (new "Reference scene bootstrap" subsection) and `src/runtime/README.md` (planned-module entry).
  - GRAPHICS-029-Impl-A/B/C are identified but explicitly **not** opened — Impl-B depends on GRAPHICS-030-Impl-A landing the `ECS::Components::ProceduralGeometryRef` type; Impl-C is gated as recorded in Decision 8.
  - No implementation, shader, renderer pass, or ECS component changes landed in this slice.
