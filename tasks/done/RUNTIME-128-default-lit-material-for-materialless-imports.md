---
id: RUNTIME-128
theme: B
depends_on: []
maturity_target: CPUContracted
---
# RUNTIME-128 — Default lit material for material-less imported primitives

## Goal
- Imported mesh primitives that have no authored material render with lighting by binding a neutral lit `StandardPBR` material instead of falling back to the unlit `DefaultDebugSurface` slot.

## Non-goals
- Changing the `DefaultDebugSurface` slot-0 contract (it stays unlit purple for genuine missing/invalid bindings per GRAPHICS-031).
- Vertex-normal computation changes (already done by `ResolveVertexNormals`).
- Any GPU object-space normal-texture bake work (owned by GRAPHICS-104).
- glTF/material authoring, PBR parameter inference, or per-primitive material variety.

## Context
- Status: done.
- Owner/agent: Claude + Codex verification/retirement.
- Completed: 2026-06-28.
- Commit: this commit (`Retire verified geometry, method, and runtime tasks`).
- Maturity: `CPUContracted`.
- Owner/layer: `runtime` (`Extrinsic.Runtime.AssetModelSceneHandoff`); material instances created through the `graphics`-owned `MaterialSystem`.
- Symptom: many imported meshes render as a flat single color that never shades.
- Root cause: a primitive whose `MaterialIndex` is out of range of the created material records binds to `Graphics::kDefaultMaterialSlotIndex` (slot 0) in `MaterializeModelSceneAsset` (`src/runtime/Runtime.AssetModelSceneHandoff.cpp:1477-1485`). Slot 0 is `Material.DefaultDebugSurface`, populated `Unlit` at `MaterialSystem::Initialize()` (GRAPHICS-031A). The forward/deferred shaders short-circuit to flat `baseColor` for `Unlit`/`DefaultDebugSurface` (`assets/shaders/forward/default_debug_surface.frag:137-138`), so the mesh never shades regardless of its vertex normals.
- Material-less imports (plain OBJ/PLY/STL, glTF primitives without a material) are a normal, expected case and must be distinguished from a genuine binding error.
- Expected behavior: material-less primitives shade with a neutral lit default; slot 0 stays the "you forgot a material" error indicator.

## Required changes
- [x] Add a lazily-created, handoff-scoped neutral lit `StandardPBR` default material to `AssetModelSceneHandoffState`, created on first use through `MaterialSystem`.
- [x] In `MaterializeModelSceneAsset` primitive binding, when a primitive has no authored material record, bind the default lit material slot instead of `kDefaultMaterialSlotIndex`; fall back to the previous slot-0 behavior only if the default lit material cannot be created (fail-open, no import failure).
- [x] Add a diagnostics counter for default lit material instances created.

## Tests
- [x] Add a CPU/null contract test in `tests/contract/runtime/Test.AssetModelSceneHandoff.cpp`: materialize a model whose primitive references no material and assert the primitive binds a valid non-default slot, the default lit material is recorded, the diagnostics counter increments, and the default material params are the neutral lit defaults (not `Unlit`). (`MaterialLessPrimitiveBindsNeutralLitDefaultMaterial`)
- [x] Execute the CPU build/test gate in the local `ci` preset build.

## Docs
- [x] Update `src/runtime/README.md` to document material-less import default-lit binding and the preserved slot-0 error semantics.

## Acceptance criteria
- [x] Material-less imported primitives bind a lit material (`HasMaterialSlot == true`, slot `!= kDefaultMaterialSlotIndex`).
- [x] Authored-material imports are unchanged (existing handoff tests still pass).
- [x] Slot 0 / `DefaultDebugSurface` remains unlit and reserved for missing/invalid bindings.
- [x] Fix introduces no layering violations and adds a regression test on the CPU gate.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R RuntimeAssetModelSceneHandoff --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Making slot 0 (`DefaultDebugSurface`) lit, or otherwise removing the missing-material visual indicator.
- Failing model import when the default lit material cannot be created.
- Adding live ECS/asset-service knowledge to graphics-owned modules.
- Mixing this fix with unrelated renderer/runtime/asset features.
