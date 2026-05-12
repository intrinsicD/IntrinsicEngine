# GRAPHICS-029A — Reference scene skeleton (interface, registry, config field)

## Goal
- Land the runtime-owned reference scene seam declared by `GRAPHICS-029` Decisions 1, 2, 7, 8, and 9: a new `Extrinsic.Runtime.ReferenceScene` module exposing `IReferenceSceneProvider`, `ReferenceSceneRegistry`, and the `EngineConfig::ReferenceScene { Enabled, Selector }` plumbing, plus the `Engine::Initialize()` invocation seam. No provider implementation lands here — the registry resolves to a no-op default so existing CPU/null tests keep observing zero renderable candidates.

> **Transition notice.** The no-op default provider this task registers is intentionally a placeholder for the gap between `GRAPHICS-029A` landing and `GRAPHICS-029B` registering `TriangleProvider` for `ReferenceSceneSelector::Triangle`. Once `GRAPHICS-029B` retires, the no-op default is **not** a permanent fallback — selectors with no provider registered should `std::terminate` (per `GRAPHICS-029` Decision 7's double-install guard policy applied to the resolve path), not silently no-op. The single explicit unit test that exercises "Resolve on an unknown selector returns the no-op default" must therefore be updated alongside `GRAPHICS-029B` to assert the terminate behavior instead. Track this as a closing-cleanup checkbox on `GRAPHICS-029B`.

## Non-goals
- No `TriangleProvider` body (that is `GRAPHICS-029B`).
- No camera substitution into `RenderFrameInput::Camera` (that is `GRAPHICS-029B`).
- No additional sample providers (`Cube`/`MultiPrimitive`/`HierarchyDemo` are `GRAPHICS-029C`).
- No GPU upload, no `GpuWorld::SetInstanceGeometry()` binding, no shader/material/pipeline change.
- No `ECS::Components::ProceduralGeometryRef` consumption (that requires `GRAPHICS-030A` first).

## Context
- Status: not started.
- Owner/layer: `runtime` for the new module; `core` for the new `EngineConfig::ReferenceScene` field (per `GRAPHICS-029` Decision 9).
- Planning parent: [`tasks/done/GRAPHICS-029-runtime-reference-scene-bootstrap.md`](../../done/GRAPHICS-029-runtime-reference-scene-bootstrap.md), Decisions 1, 2, 7, 8, 9, 11, plus the Impl-A row in Required changes.
- Source tree home: `src/runtime/Runtime.ReferenceScene.cppm` (and `.cpp` if behavior beyond the registry stub is needed). Field lives in `src/core/Core.Config.Engine.cppm` (or a new `Core.Config.ReferenceScene` partition; the implementer decides).
- Consumed by `Engine::Initialize()` immediately after `m_Scene = std::make_unique<ECS::Scene::Registry>()` and before `m_Application->OnInitialize(*this)`.
- Default-off rationale: `EngineConfig{}` keeps `Enabled = false`; only `CreateReferenceEngineConfig()` flips it to `true`.

## Required changes
- [ ] Add `EngineConfig::ReferenceScene { bool Enabled = false; ReferenceSceneSelector Selector = ReferenceSceneSelector::Triangle; }` (and the `ReferenceSceneSelector` enum) to `Core.Config.Engine`.
- [ ] Add `src/runtime/Runtime.ReferenceScene.cppm` exporting `Extrinsic.Runtime.ReferenceScene` with:
  - `struct ReferenceSceneEntity` (placeholder owned-handle list value type),
  - `struct ReferenceScenePopulation { std::vector<ReferenceSceneEntity> Entities; std::optional<Graphics::CameraViewInput> Camera; }`,
  - `class IReferenceSceneProvider` with `Populate(scene) -> ReferenceScenePopulation`, `Teardown(scene, entities)`,
  - `class ReferenceSceneRegistry` with `Register(selector, std::unique_ptr<IReferenceSceneProvider>)`, `Resolve(selector) -> IReferenceSceneProvider*`, `ResolveOrNull(selector)`.
- [ ] Wire `Engine::Initialize()` to invoke the resolved provider when `m_Config.ReferenceScene.Enabled`. Store the returned `ReferenceScenePopulation` so `Engine::Shutdown()` can call `Teardown(*m_Scene, population.Entities)`. Add a `bool m_ReferenceSceneInstalled` guard so double-install fires `std::terminate` (per `GRAPHICS-029` Decision 7 idempotency rule).
- [ ] `CreateReferenceEngineConfig()` flips `EngineConfig::ReferenceScene::Enabled = true` and `Selector = ReferenceSceneSelector::Triangle`.
- [ ] Until `GRAPHICS-029B` lands, the registry resolves to a no-op default provider (returns `ReferenceScenePopulation{}`); no entity is created.
- [ ] Wire the new module into `src/runtime/CMakeLists.txt`.

## Tests
- [ ] `contract;runtime` test: with default-constructed `EngineConfig{}` (Enabled=false), `Engine` initializes and shuts down cleanly and `RenderExtractionCache::ExtractAndSubmit()` reports `CandidateRenderableCount == 0`.
- [ ] `contract;runtime` test: with `CreateReferenceEngineConfig()` (Enabled=true) and the no-op default provider, `Engine` initializes/shutdowns cleanly without entity creation; `m_ReferenceSceneInstalled` flips to `true` once.
- [ ] `contract;runtime` test: `ReferenceSceneRegistry::Register` followed by `Resolve` returns the registered provider; `Resolve` on an unknown selector returns the no-op default.
- [ ] No `gpu`/`vulkan` tests in this slice.

## Docs
- [ ] Update `src/runtime/README.md` planned-module rows to current-state public module rows for `Extrinsic.Runtime.ReferenceScene`.
- [ ] Update `src/core/README.md` (or relevant) with the new `EngineConfig::ReferenceScene` field.
- [ ] Refresh `docs/api/generated/module_inventory.md` after adding the module surface.

## Acceptance criteria
- [ ] `Extrinsic.Runtime.ReferenceScene` compiles through the `ci` preset.
- [ ] `EngineConfig::ReferenceScene` field is plumbed end-to-end with default-off behavior preserving existing CPU/null tests.
- [ ] No frame extraction behavior change with the default provider.
- [ ] Layering checks pass: runtime imports only `Core`, `ECS::Scene::Registry`, and `Graphics.CameraSnapshots` (already an existing edge); no new graphics/RHI/asset edges.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Implementing any concrete provider body (reserved for `GRAPHICS-029B`).
- Importing `Extrinsic.Graphics.GpuWorld`, `Extrinsic.Graphics.Renderer`, `Extrinsic.Graphics.GpuAssetCache`, `Extrinsic.Graphics.MaterialSystem`, any `Extrinsic.RHI.*`, any `Extrinsic.Asset.*`, or any `Extrinsic.Platform.*` from the new module.
- Adding GPU-typed value types to `src/ecs/Components/`.
- Any renderer pass body or device backend change.

## Next verification step
- Implement the module + config field, register the no-op default provider, run the verification commands above.
