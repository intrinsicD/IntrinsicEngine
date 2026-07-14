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
- Status: done.
- Owner/agent: Claude Code agent on branch `claude/setup-agentic-workflow-stYp3` (landed via PR #810).
- Owner/layer: `runtime` for the new module; `core` for the new `EngineConfig::ReferenceScene` field (per `GRAPHICS-029` Decision 9).
- Planning parent: [`tasks/archive/GRAPHICS-029-runtime-reference-scene-bootstrap.md`](./GRAPHICS-029-runtime-reference-scene-bootstrap.md), Decisions 1, 2, 7, 8, 9, 11, plus the Impl-A row in Required changes.
- Source tree home: `src/runtime/Runtime.ReferenceScene.cppm` (and `.cpp` if behavior beyond the registry stub is needed). Field lives in `src/core/Core.Config.Engine.cppm` (or a new `Core.Config.ReferenceScene` partition; the implementer decides).
- Consumed by `Engine::Initialize()` immediately after `m_Scene = std::make_unique<ECS::Scene::Registry>()` and before `m_Application->OnInitialize(*this)`.
- Default-off rationale: `EngineConfig{}` keeps `Enabled = false`; only `CreateReferenceEngineConfig()` flips it to `true`.

## Required changes
- [x] Add `EngineConfig::ReferenceScene { bool Enabled = false; ReferenceSceneSelector Selector = ReferenceSceneSelector::Triangle; }` (and the `ReferenceSceneSelector` enum) to `Core.Config.Engine`.
- [x] Add `src/runtime/Runtime.ReferenceScene.cppm` exporting `Extrinsic.Runtime.ReferenceScene` with:
  - `struct ReferenceSceneEntity` (placeholder owned-handle list value type),
  - `struct ReferenceScenePopulation { std::vector<ReferenceSceneEntity> Entities; std::optional<Graphics::CameraViewInput> Camera; }`,
  - `class IReferenceSceneProvider` with `Populate(scene) -> ReferenceScenePopulation`, `Teardown(scene, entities)`,
  - `class ReferenceSceneRegistry` with `Register(selector, std::unique_ptr<IReferenceSceneProvider>)`, `Resolve(selector) -> IReferenceSceneProvider*`, `ResolveOrNull(selector)`.
- [x] Wire `Engine::Initialize()` to invoke the resolved provider when `m_Config.ReferenceScene.Enabled`. Store the returned `ReferenceScenePopulation` so `Engine::Shutdown()` can call `Teardown(*m_Scene, population.Entities)`. Add a `bool m_ReferenceSceneInstalled` guard so double-install fires `std::terminate` (per `GRAPHICS-029` Decision 7 idempotency rule).
- [x] `CreateReferenceEngineConfig()` flips `EngineConfig::ReferenceScene::Enabled = true` and `Selector = ReferenceSceneSelector::Triangle`.
- [x] Until `GRAPHICS-029B` lands, the registry resolves to a no-op default provider (returns `ReferenceScenePopulation{}`); no entity is created.
- [x] Wire the new module into `src/runtime/CMakeLists.txt`.

## Tests
- [x] `contract;runtime` test: with default-constructed `EngineConfig{}` (Enabled=false), `Engine` initializes and shuts down cleanly and `RenderExtractionCache::ExtractAndSubmit()` reports `CandidateRenderableCount == 0`.
- [x] `contract;runtime` test: with `CreateReferenceEngineConfig()` (Enabled=true) and the no-op default provider, `Engine` initializes/shutdowns cleanly without entity creation; `m_ReferenceSceneInstalled` flips to `true` once.
- [x] `contract;runtime` test: `ReferenceSceneRegistry::Register` followed by `Resolve` returns the registered provider; `Resolve` on an unknown selector returns the no-op default.
- [x] No `gpu`/`vulkan` tests in this slice.

## Docs
- [x] Update `src/runtime/README.md` planned-module rows to current-state public module rows for `Extrinsic.Runtime.ReferenceScene`.
- [x] Update `src/core/README.md` (or relevant) with the new `EngineConfig::ReferenceScene` field.
- [x] Refresh `docs/api/generated/module_inventory.md` after adding the module surface.

## Acceptance criteria
- [x] `Extrinsic.Runtime.ReferenceScene` compiles through the `ci` preset.
- [x] `EngineConfig::ReferenceScene` field is plumbed end-to-end with default-off behavior preserving existing CPU/null tests.
- [x] No frame extraction behavior change with the default provider.
- [x] Layering checks pass: runtime imports only `Core`, `ECS::Scene::Registry`, and `Graphics.CameraSnapshots` (already an existing edge); no new graphics/RHI/asset edges.

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

## Slice plan (active)

1. Add `EngineConfig::ReferenceScene` and the `ReferenceSceneSelector` enum to `Extrinsic.Core.Config.Engine`.
2. Author `src/runtime/Runtime.ReferenceScene.cppm` with the value types, interface, and registry (no provider body), plus `MakeDefaultReferenceSceneRegistry()` returning a registry that resolves all selectors to a no-op default provider.
3. Wire `Engine::Initialize()` / `Engine::Shutdown()` to invoke the resolved provider once, gated by `m_Config.ReferenceScene.Enabled` and protected by `m_ReferenceSceneInstalled` (double-install fires `std::terminate`).
4. Flip `CreateReferenceEngineConfig()` to set `ReferenceScene::Enabled = true`, `Selector = ReferenceSceneSelector::Triangle`.
5. Add `contract;runtime` tests covering default-off, reference-on no-op, registry register/resolve semantics, and registry double-install guard.
6. Update `src/runtime/README.md`, `src/core/README.md`, and regenerate `docs/api/generated/module_inventory.md`.
7. Run the verification commands recorded in this task.

## Completion
- Completed: 2026-05-12.
- Commit reference: `b99fed1` ("GRAPHICS-029A: reference scene skeleton + EngineConfig field"), merged to `main` via PR #810 from branch `claude/setup-agentic-workflow-stYp3`.
- Notes:
  - All "Required changes", "Tests", "Docs", and "Acceptance criteria" checkboxes were completed in the implementation slice landed by `b99fed1` / PR #810; the verification commands recorded in this task were executed in that session.
  - This commit is the mechanical retirement step missed in the implementation merge, matching the GRAPHICS-029 / GRAPHICS-030 / GRAPHICS-030A pattern. No source code, shaders, tests, or generated inventories are touched here — only the task file moves from `tasks/active/` to `tasks/done/` and the inbound `active/GRAPHICS-029A` link in `tasks/backlog/README.md`, `tasks/backlog/rendering/README.md`, and `docs/reviews/2026-05-11-sandbox-graphics-gap-analysis.md` is repointed at this `tasks/done/` file.
  - Downstream unblocked: `GRAPHICS-029B` (TriangleProvider body + camera substitution) is now the next Theme A triangle-path slice; the no-op default provider registered here remains the transition placeholder until `GRAPHICS-029B` lands the closing-cleanup checkbox documented in the Transition notice.
  - Retirement verification (run in this session):
    - `python3 tools/agents/check_task_policy.py --root . --strict`
    - `python3 tools/docs/check_doc_links.py --root .`
    - `python3 tools/repo/check_layering.py --root src --strict`
