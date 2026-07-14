---
id: BUG-050
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-050 — Direct mesh first upload lacks computed normals

## Goal
- Ensure direct mesh imports publish count-matched `v:normal` data before the
  first `GeometrySources` upload, even when the source file omitted normals.

## Non-goals
- No shader changes.
- No tangent-space normal-map implementation.
- No direct renderer/RHI calls from import or editor commands.
- No removal of the deferred direct-mesh post-process or generated normal
  texture registration path.

## Context
- Symptom: direct OBJ imports without authored normals initially publish mesh
  `GeometrySources` without `v:normal`, so the first mesh upload shades from
  the packer's +Z fallback until the deferred post-process completes.
- Expected behavior: the first renderable mesh publication already carries
  finite count-matched vertex normals; authored source normals remain
  authoritative when present.
- Impact: loading-time shading can be wrong for a newly imported mesh that
  lacks normals, even though the deferred post-process eventually computes and
  reuploads them.
- Owner: `src/runtime` import materialization plus existing geometry-owned mesh
  normal computation helpers. Runtime may compose geometry algorithms and ECS
  population; graphics remains a consumer of dirty/repacked data.

## Completion
- Completed: 2026-06-22. Commit/PR: this local fix commit.
- Root cause: the geometry-only mesh materialization helper preserved authored
  normals but deliberately returned no computed fallback normals. Direct import
  and progressive raw model-scene publication therefore depended on deferred
  enrichment before count-matched `v:normal` became visible to extraction.
- Fix summary: geometry-only mesh materialization now resolves explicit or
  area-weighted fallback normals before `PopulateFromMesh`, so first
  publication includes `v:normal` for direct imports and progressive
  model-scene raw geometry. Deferred UV/texture bake work remains intact.

## Required changes
- [x] Compute or preserve vertex normals in the geometry-only direct mesh
      materialization path before `PopulateFromMesh`.
- [x] Keep direct mesh post-process normal preservation and generated texture
      registration behavior intact.

## Tests
- [x] Update direct import coverage so no-normal OBJ imports expose finite
      count-matched `v:normal` immediately after `ImportAssetFromPath`.
- [x] Keep authored-normal direct import coverage proving imported normals are
      preserved before and after post-process.

## Docs
- [x] Update runtime docs if the loading-time normal publication contract is
      described.
- [x] Record final status in this task and the retirement log.

## Acceptance criteria
- [x] Direct mesh imports with authored normals keep those normals from first
      publication through deferred post-process.
- [x] Direct mesh imports without authored normals publish computed `v:normal`
      before the first mesh upload and keep the deferred dirty extraction path.
- [x] Fix does not introduce layering violations.

## Verification
```bash
cmake --preset ci
# Passed.

cmake --build --preset ci --target IntrinsicRuntimeContractTests -- -j4
# Passed.

ctest --test-dir build/ci --output-on-failure -R 'RuntimeAssetImportFormatCoverage.DirectObjImport(PreservesVertexNormalsInGeometrySources|ComputesVertexNormalsWhenMissing|ComputesAndBakesGeneratedNormalTextureWhenMissingNormals)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 3/3 tests.

ctest --test-dir build/ci --output-on-failure -R 'RuntimeAssetModelSceneHandoff\.ProgressiveRawGeometryFirstPublishesNormalsAndQueuesUvAndBakeJobs' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 1/1 test.

cmake --build --preset ci --target IntrinsicTests -- -j4
# Passed.

ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 4980/4980 tests.

python3 tools/repo/check_layering.py --root src --strict
# Passed.

python3 tools/agents/check_task_policy.py --root . --strict
# Passed after retirement.
```

## Forbidden changes
- Shipping a fix without a regression test when one is feasible.
- Reintroducing fragment-shader normal texture sampling.
- Treating fallback +Z packing as successful computed normal publication.

## Maturity
- Target: `CPUContracted`.
- The default CPU/null runtime import and extraction contracts prove this bug;
  no `Operational` Vulkan follow-up is owed because GPU visibility continues
  through the existing dirty-tag extraction and upload-barrier paths.
