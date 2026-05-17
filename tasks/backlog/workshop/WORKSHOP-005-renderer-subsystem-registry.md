# WORKSHOP-005 — Extract renderer subsystem registry and lifecycle ownership

## Goal
- Reduce `Graphics.Renderer.cpp` god-object pressure by moving renderer subsystem/resource-manager ownership and lifecycle order into a dedicated renderer subsystem registry.

## Non-goals
- Do not change render graph compilation or pass recipe semantics.
- Do not change runtime extraction behavior.
- Do not implement new rendering features.
- Do not split every renderer concern in one giant refactor.

## Context
- The current renderer implementation owns many managers/systems directly: RHI managers, `GpuWorld`, material/colormap/visualization/culling/transform/light/selection/forward/deferred/postprocess/shadow systems, pass pipeline leases, and lifecycle diagnostics.
- This is manageable now but will become the new spaghetti center if every feature adds one more member and one more lifecycle branch.
- This task creates a stable containment seam without changing behavior.

## Required changes
- [ ] Introduce a dedicated renderer subsystem registry/lifecycle class, for example `Graphics.RenderSubsystemRegistry` or `Graphics.RenderResourceSystemRegistry`.
- [ ] Move ownership of these objects into the registry where appropriate:
  - `RHI::BufferManager`
  - `RHI::TextureManager`
  - `RHI::SamplerManager`
  - `RHI::PipelineManager`
  - `GpuWorld`
  - `MaterialSystem`
  - `ColormapSystem`
  - `VisualizationSyncSystem`
  - `CullingSystem`
  - `TransformSyncSystem`
  - `LightSystem`
  - `SelectionSystem`
  - `ForwardSystem`
  - `DeferredSystem`
  - `PostProcessSystem`
  - `ShadowSystem`
- [ ] Make initialization and shutdown order explicit in one place.
- [ ] Preserve `IRenderer` accessors initially by forwarding through the registry.
- [ ] Move operational-resource rebuild helper ownership into the registry if it only touches subsystem resources.
- [ ] Keep frame lifecycle methods behavior-equivalent.
- [ ] Add diagnostics for partially initialized registry state.
- [ ] Keep patches mechanical where possible; avoid mixing this extraction with semantic pass changes.

## Tests
- [ ] Add a unit/contract test proving subsystem init/shutdown order is deterministic.
- [ ] Add a test proving `Shutdown()` is safe after partial initialization failure.
- [ ] Add a test proving `RebuildOperationalResources()` fails closed when required subsystems are missing.
- [ ] Update existing renderer frame lifecycle tests to use the registry path.
- [ ] Keep existing renderer/RHI manager tests passing.

## Docs
- [ ] Update rendering architecture docs to describe renderer subsystem registry ownership.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` graphics/renderer row.
- [ ] Update generated module inventory if public module surfaces changed.

## Acceptance criteria
- [ ] `Graphics.Renderer.cpp` no longer directly owns the full list of subsystem optionals/managers.
- [ ] Subsystem lifecycle order is explicit and tested.
- [ ] Existing `IRenderer` public API remains source-compatible unless a deliberate API change is documented.
- [ ] No new layer dependencies are introduced.

## Verification
```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L "unit|contract" -LE "gpu|vulkan|slow|flaky-quarantine" --timeout 60 -j$(nproc)
```

## Forbidden changes
- Do not change renderer-visible behavior to make extraction easier.
- Do not remove existing diagnostics.
- Do not introduce runtime/ECS dependencies into graphics.
- Do not combine this with pass routing or recipe refactors.
