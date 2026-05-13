# GRAPHICS-029B — TriangleProvider and reference camera substitution

## Goal
- Implement the `TriangleProvider` body and the runtime camera substitution declared by `GRAPHICS-029` Decisions 3 and 4: with `EngineConfig::ReferenceScene::Enabled = true` and `Selector = Triangle`, runtime creates exactly one renderable entity (`MetaData{ EntityName = "ReferenceTriangle" }`, `Transform::Component{}`, `Transform::WorldMatrix{}`, `Hierarchy::Component{}`, `Graphics::Components::RenderSurface{ Domain = Vertex }`, plus `ECS::Components::ProceduralGeometryRef{ Triangle }` once `GRAPHICS-030A` is complete) and seeds `RenderFrameInput::Camera` with the provider's `CameraViewInput`.

> **Intermediate-solution notice.** Two artifacts introduced by this task are intermediate, with explicit retirement arcs:
>
> 1. **The direct `m_ReferenceCamera` substitution into `RenderFrameInput::Camera`** is replaced by [`RUNTIME-081`](RUNTIME-081-camera-controllers.md) (CameraControllers umbrella). Once `RUNTIME-081` lands, the reference-scene-provided `CameraViewInput` becomes only the *seed* for the active controller's initial state; per-frame updates come from `controller->Update(input, dt)` + `controller->GetView(viewport)`. The renderer/runtime call site that substitutes `m_ReferenceCamera` directly into `RenderFrameInput::Camera` must be retired by `RUNTIME-081`. Implement the substitution today as a clearly-marked transitional code path (e.g. `// TODO(RUNTIME-081): superseded by controller-driven update`) so the retirement is mechanical.
> 2. **The `#if __has_include(...)` (or CMake-flag) test guard around the `ProceduralGeometryRef` assertion** exists only because `GRAPHICS-030A` may still be in flight when `GRAPHICS-029B` lands. Once `GRAPHICS-030A` retires to `tasks/done/`, the guard must be removed and the test becomes unconditional. Track this as a closing-cleanup checkbox on `GRAPHICS-030A`'s retirement notice.
>
> Neither artifact introduces a permanent design — they are bridges over scheduling gaps.

## Non-goals
- No GPU upload, no `GpuWorld::SetInstanceGeometry()` binding (that is `GRAPHICS-030B`).
- No additional providers beyond `Triangle` (those are `GRAPHICS-029C`).
- No light, shadow, or material binding beyond the GRAPHICS-031 default debug surface contract (lit policy is deferred per `GRAPHICS-029` Decision 5).
- No camera-input or camera-controller wiring (that umbrella is `Extrinsic.Runtime.CameraControllers`, `RUNTIME-002`).
- No editor/UI integration.

## Context
- Status: in-progress.
- Owner/agent: Claude Code agent on branch `claude/setup-agentic-workflow-QcZwZ`.
- Owner/layer: `runtime`.
- Planning parent: [`tasks/done/GRAPHICS-029-runtime-reference-scene-bootstrap.md`](../done/GRAPHICS-029-runtime-reference-scene-bootstrap.md), Decisions 3, 4, 5, 6, 7, 10. Recorded as Impl-B in the parent's Required changes.
- Upstream gates: `GRAPHICS-029A` (done; skeleton, registry, EngineConfig::ReferenceScene field, no-op default placeholder). `GRAPHICS-030A` (done; `ECS::Components::ProceduralGeometryRef` value type) — `TriangleProvider` attaches the ref unconditionally; the `#if __has_include(...)` guard mentioned in the original task body is no longer required (closing-cleanup checkbox below).
- Camera defaults: `Position = (0, 0, 3)`, `Forward = (0, 0, -1)`, `Up = (0, 1, 0)`, perspective with 45° vertical FoV, aspect derived from `RenderFrameInput::Viewport`, near 0.1, far 100. `CameraViewInput::Valid = true`.

## Required changes
- [x] Implement `Extrinsic::Runtime::TriangleProvider` (declaration in `Runtime.ReferenceScene.cppm`, body in `Runtime.ReferenceScene.cpp`):
  - `Populate(scene)` calls `ECS::Scene::CreateDefault(scene, "ReferenceTriangle")` (HARDEN-060) and attaches `Graphics::Components::RenderSurface{ Domain = SourceDomain::Vertex }`.
  - Attaches `ECS::Components::ProceduralGeometryRef{ Kind = ProceduralGeometryKind::Triangle }` unconditionally (GRAPHICS-030A is done, so the originally-planned `#if __has_include(...)` guard is omitted).
  - Returns `ReferenceScenePopulation{ {entity}, CameraViewInput{...defaults above...} }`.
  - `Teardown(scene, entities)` destroys each entity in `entities` from the scene registry.
- [x] Register `TriangleProvider` for `ReferenceSceneSelector::Triangle` in the registry seam exposed by `GRAPHICS-029A` via `MakeDefaultReferenceSceneRegistry()` and `RegisterDefaultReferenceProvidersIfAbsent(registry)`; the latter is invoked by `Engine::Initialize()` before resolve so test-side `Register()` and engine-side defaults compose under the strict double-install guard.
- [x] Tighten `ReferenceSceneRegistry::Resolve()` to `std::terminate` for unregistered selectors (closing cleanup carried over from GRAPHICS-029A Transition notice). The previous no-op default provider is retired.
- [x] Extend `Engine::Initialize()` to record the population's optional `CameraViewInput` on `m_ReferenceCamera` (with matching `Engine::GetReferenceCameraSeed()` accessor for the contract test seam).
- [x] Extend `Engine::RunFrame()` so that when no other camera source is wired, `RenderFrameInput::Camera` is substituted from `BuildReferenceCameraViewInput(*m_ReferenceCamera, viewport)` (free function exported by `Extrinsic.Runtime.ReferenceScene` that derives `View` from `glm::lookAt` and `Projection` from `glm::perspective(45° fovY, viewport-aspect, near, far)` and flips `Projection[1][1]` for Vulkan clip-space Y inversion, matching `src/legacy/Graphics/Graphics.Camera.cpp:34-39`). Call site carries a `// TODO(RUNTIME-081)` retirement comment per the Intermediate-solution notice.
- [x] Idempotency: the entity is created once at `Initialize()`; `Engine::Shutdown()` clears `m_ReferenceCamera` alongside the provider teardown so re-initialise loops do not republish stale state.

## Tests
- [x] `contract;runtime` test: with `CreateReferenceEngineConfig()`-equivalent `EngineConfig` (Enabled=true, Selector=Triangle), after `Engine::Initialize()` the scene contains exactly one entity with `MetaData::EntityName == "ReferenceTriangle"` and the expected component set (`Transform::Component`, `Transform::WorldMatrix`, `Hierarchy::Component`, `Graphics::Components::RenderSurface{Domain=Vertex}`, `ECS::Components::ProceduralGeometryRef{Kind=Triangle}`), and `RenderExtractionCache::ExtractAndSubmit()` reports `CandidateRenderableCount == 1` and `AllocatedInstanceCount == 1`.
- [x] `contract;runtime` test: `BuildReferenceCameraViewInput(seed, viewport)` produces a `CameraViewInput` whose `Position`/`Forward`/`Up`/`NearPlane`/`FarPlane` match the recorded defaults and whose `Projection` row ratio `Projection[1][1] / Projection[0][0]` equals `viewport.Width / viewport.Height`. The result passes the GRAPHICS-002 sanitizer (`Graphics::BuildCameraViewSnapshot(...).Valid == true`).
- [x] `contract;runtime` test: the bootstrap entity carries `ProceduralGeometryRef{ Kind == Triangle }` unconditionally (GRAPHICS-030A is done; the originally-planned `#if __has_include(...)` guard is unnecessary).
- [x] `contract;runtime` test: on `Engine::Shutdown()` the bootstrap entity is destroyed (provider `Teardown` runs before scene-registry destruction) and `Engine::GetReferenceCameraSeed()` returns `std::nullopt`.
- [x] `contract;runtime` test: `ReferenceSceneRegistry::Resolve()` on an unregistered selector fires `std::terminate` (`EXPECT_DEATH`), replacing the GRAPHICS-029A "no-op default fallback" test.
- [x] No `gpu`/`vulkan` tests in this slice.

## Docs
- [x] Update `src/runtime/README.md` to document the `TriangleProvider` registration, the `RegisterDefaultReferenceProvidersIfAbsent` seam, and the reference-camera substitution flow.
- [x] Note in the README that `RUNTIME-081`/`RUNTIME-002` (`CameraControllers`) will consume the `CameraViewInput` seed as initial state when wired, and that the `Engine::RunFrame()` substitution call site is the mechanical retirement anchor.

## Acceptance criteria
- [x] With `EngineConfig::ReferenceScene::Enabled = true`, `RenderExtractionCache::ExtractAndSubmit()` reports exactly one renderable candidate per frame.
- [x] The seeded `CameraViewInput` is finite, invertible when used, and satisfies the GRAPHICS-002 sanitizer (`Graphics::BuildCameraViewSnapshot(...).Valid == true`).
- [x] `Runtime.ReferenceScene` imports no graphics service / RHI / asset-service modules. New imports are limited to CPU-only value-type modules (`Extrinsic.ECS.Component.{MetaData,Hierarchy,Transform,Transform.WorldMatrix,ProceduralGeometryRef}`, `Extrinsic.ECS.Scene.Bootstrap`, `Extrinsic.Graphics.Component.RenderGeometry`, `Extrinsic.Platform.Window` for `Extent2D`); no `Extrinsic.Graphics.GpuWorld`, `Extrinsic.Graphics.Renderer`, `Extrinsic.Graphics.GpuAssetCache`, `Extrinsic.Graphics.MaterialSystem`, `Extrinsic.RHI.*`, or `Extrinsic.Asset.*` imports were added.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing camera input/controller code.
- Any GPU upload, geometry binding, material registration, or pipeline change.
- Importing `Extrinsic.Platform.*` (input must arrive through later runtime/platform seams).
- Adding GPU-typed value types to `src/ecs/Components/`.

## Next verification step
- Land `TriangleProvider` and the camera substitution, exercise the new contract tests, run the verification commands above.

## Slice plan (active)

1. Add `TriangleProvider` (declaration in `Runtime.ReferenceScene.cppm`, body in `Runtime.ReferenceScene.cpp`) and pull in the value-type ECS/Graphics component imports needed for `MetaData`, `Hierarchy::Component`, `Transform::Component`, `Transform::WorldMatrix`, `RenderSurface`, and `ProceduralGeometryRef` (CPU-only POD modules — no graphics service / RHI / asset-service imports).
2. Tighten `ReferenceSceneRegistry::Resolve` to `std::terminate` for unregistered selectors per the `GRAPHICS-029A` Transition notice and the GRAPHICS-029 Decision 7 invariant: remove the no-op default member and replace `MakeDefaultReferenceSceneRegistry()` with a factory that pre-registers `TriangleProvider` for `ReferenceSceneSelector::Triangle`. Add `RegisterDefaultReferenceProvidersIfAbsent(registry)` so engine-side defaults can be installed without colliding with test-side `Register()` calls under the strict double-install guard.
3. Add `BuildReferenceCameraViewInput(seed, viewport) -> CameraViewInput` as a free function that finalises `View`/`Projection` from the seeded defaults and the runtime viewport (45° vertical FoV, derived aspect ratio, near 0.1, far 100); the seed itself carries `Position`/`Forward`/`Up`/`NearPlane`/`FarPlane`/`Valid=true` only.
4. Wire `Engine::Initialize()` to (a) call `RegisterDefaultReferenceProvidersIfAbsent`, (b) capture `ReferenceScenePopulation::Camera` into `m_ReferenceCamera`. Wire `Engine::RunFrame()` to substitute `renderInput.Camera` from `BuildReferenceCameraViewInput(*m_ReferenceCamera, viewport)` with a clearly-marked `// TODO(RUNTIME-081)` transitional comment so the future retirement is mechanical.
5. Replace the GRAPHICS-029A no-op fallback tests (`DefaultProviderReturnsEmptyPopulation`, `ResolveOnUnknownSelectorReturnsNullptrButResolveFallsBack`, `ReferenceEnabledWithNoExplicitProviderFallsBackToNoopAndCreatesNoEntities`) with the tightened semantics. Add new `contract;runtime` tests for: (i) `TriangleProvider` entity name + component set + extraction stats (`CandidateRenderableCount == 1`, `AllocatedInstanceCount == 1`), (ii) camera defaults + `BuildCameraViewSnapshot(...)` sanitiser passes (`snapshot.Valid == true`, aspect from viewport), (iii) shutdown teardown destroys the bootstrap entity, (iv) `Resolve()` on an unregistered selector terminates.
6. Update `src/runtime/README.md` with the `TriangleProvider`, default-providers seam, and camera-substitution transition note (mentions `RUNTIME-081`/`RUNTIME-002` consume `CameraViewInput` as a seed once wired). Regenerate `docs/api/generated/module_inventory.md`.
7. Run the verification commands recorded in this task.

## Closing cleanup carried over from GRAPHICS-029A's Transition notice

- [x] After `TriangleProvider` registration in `MakeDefaultReferenceSceneRegistry()`, the GRAPHICS-029A `ReferenceSceneRegistry::Resolve(...)` no-op fallback is retired: `Resolve(unregistered_selector)` now fires `std::terminate`, matching GRAPHICS-029 Decision 7's double-install guard policy applied to the resolve path. The existing "Resolve on an unknown selector returns the no-op default" test is replaced by an `EXPECT_DEATH(...)` test that constructs a fresh empty registry and resolves an unregistered selector.
- [x] The `#if __has_include(...)` (or CMake-flag) guard around the `ProceduralGeometryRef` assertion is omitted entirely because `GRAPHICS-030A` retired to `tasks/done/` before this slice landed; the test asserts the ref unconditionally.

## Completion
- Completed: 2026-05-12.
- Commit references: `3a7102c` ("GRAPHICS-029B: TriangleProvider, default registration, and reference camera substitution") and `46cb3bb` ("GRAPHICS-029B: flip Projection[1][1] for Vulkan clip-space Y in reference camera"), merged to `main` via PR #812 from branch `claude/setup-agentic-workflow-QcZwZ`.
- Notes:
  - All "Required changes", "Tests", "Docs", "Acceptance criteria", and the "Closing cleanup carried over from GRAPHICS-029A's Transition notice" checkboxes were completed in the implementation slices landed by `3a7102c` and `46cb3bb` / PR #812; the verification commands recorded in this task were executed in those sessions.
  - `Runtime.ReferenceScene.cpp:115` invokes `std::terminate()` for unregistered selectors and `tests/contract/runtime/Test.RuntimeReferenceScene.cpp` covers the behavior via `EXPECT_DEATH` cases; the `ProceduralGeometryRef` assertion is unconditional (no `__has_include` guard) because `GRAPHICS-030A` retired before this slice.
  - This commit is the mechanical retirement step missed in the implementation merge, matching the GRAPHICS-029 / GRAPHICS-029A / GRAPHICS-030 / GRAPHICS-030A pattern. No source code, shaders, tests, or generated inventories are touched here — only the task file moves from `tasks/active/` to `tasks/done/` and the inbound `active/GRAPHICS-029B` links in `tasks/backlog/README.md`, `tasks/backlog/rendering/README.md`, and `docs/reviews/2026-05-11-sandbox-graphics-gap-analysis.md` are repointed at this `tasks/done/` file.
  - Downstream unblocked: per the Theme A triangle-path DAG, `GRAPHICS-030B` (extraction → procedural geometry binding) and `GRAPHICS-031A` (default debug surface shaders/pipeline) are the next slices; `RUNTIME-081` (CameraControllers) inherits the `Engine::RunFrame()` `// TODO(RUNTIME-081)` retirement anchor for the direct `m_ReferenceCamera` substitution.
  - Retirement verification (run in this session):
    - `python3 tools/agents/check_task_policy.py --root . --strict`
    - `python3 tools/docs/check_doc_links.py --root .`
