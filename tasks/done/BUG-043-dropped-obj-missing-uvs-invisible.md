---
id: BUG-043
theme: none
depends_on: []
maturity_target: CPUContracted
---
# BUG-043 — Dropped OBJ without UVs loads but is invisible

## Goal
- Make dragged or directly imported OBJ mesh assets that omit authored texture coordinates materialize with finite `v:texcoord` data so the surface mesh render path can upload and draw them.

## Non-goals
- No xatlas backend implementation.
- No atlas quality, packing, seam, or distortion guarantee beyond finite fallback texture coordinates.
- No renderer-side UV fabrication.
- No change to the rule that surface mesh U/V carries texture coordinates only.
- No graphics, Vulkan, platform, or UI ownership changes.

## Context
- Owning subsystem/layer: `runtime` asset materialization and `GeometrySources` population.
- Current symptom: drag/drop can decode and register an OBJ asset, but an OBJ without `vt` lines reaches render extraction without `v:texcoord`; `Runtime.MeshGeometryPacker` then fails closed with `MissingTexcoords`, so no surface geometry is uploaded.
- The long-term policy remains owned by `ASSETIO-008`: xatlas-backed default UV atlas materialization with backend selection and diagnostics. This bug is the narrow visibility repair that keeps imports usable until that task lands.
- Fallback UVs must be written before generated texture bakes and before ECS `GeometrySources` population so runtime-generated normal/albedo textures and renderer material sampling use the same coordinates.

## Completion
- Completed: 2026-06-14. Commit/PR: this retirement commit.
- Root cause: no-UV OBJ payloads were successfully decoded and materialized as
  entities, but runtime mesh materialization did not supply `v:texcoord`; the
  strict `Runtime.MeshGeometryPacker` then rejected the mesh as
  `MissingTexcoords`, so render extraction never uploaded surface geometry.
- Fix summary: `BuildRuntimeHalfedgeMeshWithNormals(...)` now preserves valid
  authored `v:texcoord` and writes deterministic finite projection fallback UVs
  when source texture coordinates are absent or invalid. The fallback runs
  before direct import ECS population, before model-scene primitive handoff, and
  before generated normal/albedo texture bakes. The renderer packer remains
  fail-closed and never fabricates surface UVs.

## Required changes
- [x] Add a runtime-owned fallback UV generation step for imported meshes whose decoded payload lacks valid finite one-UV-per-vertex `v:texcoord`.
- [x] Preserve valid authored `v:texcoord` values unchanged.
- [x] Apply the fallback before direct mesh import ECS materialization and model-scene primitive handoff.
- [x] Keep `Runtime.MeshGeometryPacker` fail-closed on missing/invalid `v:texcoord` so renderer-visible mesh U/V remains texture-coordinate-only.

## Tests
- [x] Add a regression test for direct OBJ import without `vt` lines proving the mesh entity gets finite `v:texcoord`.
- [x] Add extraction coverage proving that same import uploads surface geometry instead of reporting `MeshGeometryMissingTexcoords`.
- [x] Keep existing authored-UV import coverage passing.

## Docs
- [x] Update `src/runtime/README.md` to describe the temporary geometry-projected fallback and the `ASSETIO-008` replacement path.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after retiring this bug task.

## Acceptance criteria
- [x] OBJ files without authored UVs become visible through the CPU/null render extraction path after direct import or drag/drop materialization.
- [x] The generated fallback writes finite `v:texcoord`, not encoded normals and not all-zero placeholder data.
- [x] Authored UVs remain the preferred source when present and valid.
- [x] Long-term xatlas/default-backend work remains tracked by `ASSETIO-008` and is not claimed complete here.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
# Passed.

ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi.EngineImportFacadeMaterializesObjWithoutAuthoredTexcoordsAsRenderableMesh|RuntimeAssetModelSceneHandoff.MissingTexcoordsReceiveFallbackAndGenerateNormalTexture' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 2/2 tests.

ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi.*Obj|EngineImportFacadeMaterializesStandaloneGeometryDomains|DroppedFilePathsRoute|RuntimeAssetModelSceneHandoff|MeshGeometryPacker|MeshGeometryExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 64/64 tests.

cmake --build --preset ci --target IntrinsicTests
# Passed.

ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 3018/3018 tests.

python3 tools/agents/generate_session_brief.py --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict --files $(git diff --name-only --diff-filter=ACMR) $(git ls-files --others --exclude-standard)
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
# Passed.
```

## Forbidden changes
- Do not reintroduce oct-encoded normals into surface mesh U/V.
- Do not make graphics, Vulkan, or shaders invent UVs.
- Do not add live `AssetService`/graphics handles to lower layers or ECS components.
- Do not retire or weaken `ASSETIO-008`, `GEOM-025`, or `GRAPHICS-088`.

## Maturity
- Target: `CPUContracted`.
- This bug fixes the runtime CPU/null visibility contract. `Operational` owned by `ASSETIO-008`; the atlas backend proof is owned by `GEOM-025`.
