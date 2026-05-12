# GRAPHICS-029B — TriangleProvider and reference camera substitution

## Goal
- Implement the `TriangleProvider` body and the runtime camera substitution declared by `GRAPHICS-029` Decisions 3 and 4: with `EngineConfig::ReferenceScene::Enabled = true` and `Selector = Triangle`, runtime creates exactly one renderable entity (`MetaData{ EntityName = "ReferenceTriangle" }`, `Transform::Component{}`, `Transform::WorldMatrix{}`, `Hierarchy::Component{}`, `Graphics::Components::RenderSurface{ Domain = Vertex }`, plus `ECS::Components::ProceduralGeometryRef{ Triangle }` once `GRAPHICS-030A` is complete) and seeds `RenderFrameInput::Camera` with the provider's `CameraViewInput`.

> **Intermediate-solution notice.** Two artifacts introduced by this task are intermediate, with explicit retirement arcs:
>
> 1. **The direct `m_ReferenceCamera` substitution into `RenderFrameInput::Camera`** is replaced by [`RUNTIME-081`](../runtime/RUNTIME-081-camera-controllers.md) (CameraControllers umbrella). Once `RUNTIME-081` lands, the reference-scene-provided `CameraViewInput` becomes only the *seed* for the active controller's initial state; per-frame updates come from `controller->Update(input, dt)` + `controller->GetView(viewport)`. The renderer/runtime call site that substitutes `m_ReferenceCamera` directly into `RenderFrameInput::Camera` must be retired by `RUNTIME-081`. Implement the substitution today as a clearly-marked transitional code path (e.g. `// TODO(RUNTIME-081): superseded by controller-driven update`) so the retirement is mechanical.
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
- Status: not started.
- Owner/layer: `runtime`.
- Planning parent: [`tasks/done/GRAPHICS-029-runtime-reference-scene-bootstrap.md`](../../done/GRAPHICS-029-runtime-reference-scene-bootstrap.md), Decisions 3, 4, 5, 6, 7, 10. Recorded as Impl-B in the parent's Required changes.
- Upstream gates: `GRAPHICS-029A` (skeleton must exist). Carrying `ProceduralGeometryRef` requires `GRAPHICS-030A` (active) — until that lands, the bootstrap entity carries the render hint only and `RenderExtraction` reports it as a candidate without a geometry handle, exactly as the parent task records.
- Camera defaults: `Position = (0, 0, 3)`, `Forward = (0, 0, -1)`, `Up = (0, 1, 0)`, perspective with 45° vertical FoV, aspect derived from `RenderFrameInput::Viewport`, near 0.1, far 100. `CameraViewInput::Valid = true`.

## Required changes
- [ ] Implement `Extrinsic::Runtime::TriangleProvider` (declaration in `Runtime.ReferenceScene.cppm`, body in `Runtime.ReferenceScene.cpp`):
  - `Populate(scene)` calls `ECS::Scene::CreateDefault(scene, "ReferenceTriangle")` (HARDEN-060) and attaches `Graphics::Components::RenderSurface{ Domain = SourceDomain::Vertex }`.
  - When `ECS::Components::ProceduralGeometryRef` is available, also attach `ProceduralGeometryRef{ Kind = ProceduralGeometryKind::Triangle }`.
  - Returns `ReferenceScenePopulation{ {entity}, CameraViewInput{...defaults above...} }`.
  - `Teardown(scene, entities)` destroys each entity in `entities` from the scene registry.
- [ ] Register `TriangleProvider` for `ReferenceSceneSelector::Triangle` in the registry seam exposed by `GRAPHICS-029A`.
- [ ] Extend `Engine::Initialize()` to record the population's optional `CameraViewInput` (e.g. `m_ReferenceCamera`).
- [ ] Extend `Engine::RunFrame()` (or a new `BuildRenderFrameInput()` helper) so that when no other camera source is wired, `RenderFrameInput::Camera` is substituted from `m_ReferenceCamera` after deriving the viewport-aware aspect ratio.
- [ ] Idempotency: do not re-populate per frame; the entity is created once at `Initialize()`.

## Tests
- [ ] `contract;runtime` test: with `CreateReferenceEngineConfig()`, after `Engine::Initialize()`, the scene contains exactly one entity with `MetaData::EntityName == "ReferenceTriangle"`, the expected components attached, and `RenderExtractionCache::ExtractAndSubmit()` reports `CandidateRenderableCount == 1` and `AllocatedInstanceCount == 1`.
- [ ] `contract;runtime` test: `RenderFrameInput::Camera.Valid == true`, position/forward/up match the recorded defaults, `Aspect` matches `Viewport.Width / Viewport.Height`.
- [ ] `contract;runtime` test (gated): once `GRAPHICS-030A` lands, the bootstrap entity also carries `ProceduralGeometryRef{ Kind == Triangle }`. Until then, the test asserts the absence is intentional via a `#if __has_include(...)` style guard or a CMake-level build flag.
- [ ] `contract;runtime` test: on `Engine::Shutdown()`, the entity is destroyed before scene teardown.
- [ ] No `gpu`/`vulkan` tests in this slice.

## Docs
- [ ] Update `src/runtime/README.md` to document the `TriangleProvider` registration and the camera substitution flow.
- [ ] Note in the README that `RUNTIME-002` (`CameraControllers`) consumes `CameraViewInput` as a seed when wired.

## Acceptance criteria
- [ ] With `CreateReferenceEngineConfig()`, the renderer's `RenderWorld::Renderables` contains exactly one record per frame.
- [ ] The seeded `CameraViewInput` is finite, invertible (when used), and satisfies the GRAPHICS-002 sanitizer.
- [ ] No graphics/RHI/asset import was added to `Runtime.ReferenceScene`.

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
