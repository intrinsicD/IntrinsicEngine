---
id: RUNTIME-108
theme: none
depends_on: []
maturity_target: CPUContracted
---
# RUNTIME-108 — Remove mesh UV normal fallback

## Goal
- Remove the runtime mesh surface fallback that writes oct-encoded normals into
  `MeshVertex::U/V`; mesh surface UVs must be texture coordinates only.

## Non-goals
- No new parameterization backend or asset import fallback implementation.
- No shader/PBR feature expansion beyond preserving the existing UV vertex channel contract.
- No generic texture-bake UI or user command surface.
- No Vulkan-only proof in this slice.
- No change to mesh edge/vertex point-lane or point-cloud impostor payloads that
  intentionally use their point-rendering UV lane for normals.
- No seam-split/source-xref work; generated/render atlas materialization remains
  owned by `ASSETIO-008`, `GEOM-025`, and `GRAPHICS-088`.

## Context
- Owning subsystem/layer: `runtime` extraction and mesh residency; graphics consumes the resulting vertex bytes and snapshots.
- `Runtime.MeshGeometryPacker` packed `{position.xyz, U, V}` and fell back to oct-encoded normals in `U,V` when `v:texcoord` was missing. That conflicts with the invariant that rendered mesh UVs are always actual texture coordinates.
- This slice closes the immediate regression by making missing, count-mismatched,
  and non-finite mesh surface texcoords fail closed with explicit pack/extraction
  diagnostics.
- `ASSETIO-008` resolves UVs for imported assets that arrive without them. Until
  that lands, such meshes may import as CPU geometry but cannot render as mesh
  surfaces.

## Completion
- Completed: 2026-06-13. Commit/PR: this retirement commit.
- Fix summary: `MeshGeometryPacker` now treats `MeshVertex::U/V` as texture
  coordinates only, reports `MissingTexcoords` for absent, wrong-typed, or
  count-mismatched `v:texcoord`, reports `NonFiniteTexcoord` for NaN/infinity
  UV payloads, and applies the same UV validity gate to
  `BuildSurfaceTriangleFaceMap`.
- Runtime extraction now records missing/non-finite mesh texcoord counters and
  skips unrenderable surface uploads instead of fabricating UVs from normals.
- Remaining work: generated atlas/materialization stays with `ASSETIO-008` and
  `GEOM-025`, renderer operational proof stays with `GRAPHICS-088`, and generic
  bake expansion stays with `RUNTIME-109`.

## Required changes
- [x] Add `MissingTexcoords` and `NonFiniteTexcoord` failure statuses to `Runtime.MeshGeometryPacker`.
- [x] Remove the oct-encoded-normal fallback from the mesh surface UV channel; `MeshVertex::U/V` must always be texture coordinates.
- [x] Ensure runtime-authored procedural/reference meshes populate valid `v:texcoord` before extraction.
- [x] Add extraction diagnostics counters for missing and non-finite mesh surface UV states.
- [x] Make runtime extraction fail closed for mesh surface render requests that still lack valid `v:texcoord`, while keeping line/point-only render domains unaffected.
- [x] Update direct ECS authoring helpers or tests so renderable mesh fixtures include valid texture coordinates.

## Tests
- [x] Add `unit;runtime` or `integration;runtime` packer tests proving `PackMesh` succeeds with finite `v:texcoord`.
- [x] Add fail-closed tests for missing, count-mismatched, and non-finite `v:texcoord`.
- [x] Add regression coverage proving the UV channel is no longer populated from oct-encoded normals.
- [x] Add extraction diagnostics tests proving missing/invalid UV surface requests are counted and skipped.
- [x] Add procedural/reference scene tests proving built-in meshes satisfy the UV invariant.

## Docs
- [x] Update `src/runtime/README.md` for the resolved-UV render residency invariant.
- [x] Update `src/graphics/renderer/README.md` if the vertex format description changes.
- [x] Update tests/docs if any new diagnostics counters or statuses are public.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module surface changes.

## Acceptance criteria
- [x] Runtime mesh surface extraction never renders a mesh by substituting normals into the UV channel.
- [x] Renderable mesh surfaces without valid texture coordinates fail closed with explicit diagnostics.
- [x] Reference-scene meshes and test fixtures pass the new UV-required packer contract.
- [x] `geometry`, `assets`, and `graphics` layering remains unchanged.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
# Passed.

ctest --test-dir build/ci --output-on-failure -R 'MeshGeometryPacker|MeshGeometryExtraction|MeshPrimitiveViewExtraction|PrimitiveSelectionRefinement|RuntimeReferenceScene|SandboxEditorUi|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 150/150 tests.

cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
# Passed. GPU smoke binary compile-checked only; the `gpu;vulkan` test was not run.

ctest --test-dir build/ci --output-on-failure -R 'RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 8/8 tests.

ctest --test-dir build/ci --output-on-failure -R 'TriangleProviderContract.AuthoredTriangleSceneDocumentRoundTripsFullRenderableContract|RuntimeSceneSerialization|ReferenceScene|EngineReferenceScene' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 15/15 tests.

python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
# Passed: wrote docs/api/generated/module_inventory.md (484 modules).

cmake --build --preset ci --target IntrinsicTests
# Passed.

ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 3015/3015 tests.

python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
# Passed.
```

## Forbidden changes
- Do not reintroduce implicit UV fallbacks in graphics or shaders.
- Do not fail line-only or point-only render paths because mesh UVs are absent.
- Do not store live graphics handles in ECS to solve provenance.
- Do not bypass runtime extraction diagnostics when skipping unrenderable surfaces.

## Maturity
- Closed at `CPUContracted`.
- This task closes the CPU/null extraction and packing contract. `Operational` owned by `GRAPHICS-088`.
