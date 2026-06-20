---
id: BUG-048
theme: G
depends_on: []
---
# BUG-048 — Direct mesh post-process overwrites recomputed normals

## Goal
- Keep editor-recomputed mesh vertex normals authoritative when a pending direct mesh post-process completes after the edit.

## Non-goals
- No removal of generated normal texture registration.
- No tangent-space normal-map implementation.
- No change to the shader policy from `BUG-047`.
- No immediate GPU upload from editor commands.

## Context
- Symptom: after recomputing mesh vertex normals in the sandbox editor, shading can take a long time to reflect the new normals or appear to revert.
- Expected behavior: an editor normal recompute updates the mesh `v:normal` property, marks vertex attributes dirty, and the next render extraction/GPU upload sees that property without a later background task replacing it.
- Impact: direct mesh imports queue `Runtime.DirectMeshPostProcess`; its main-thread apply path called `PopulateFromMesh` again after the user edit, restoring the import/materialization normals and delaying visible corrected shading until another edit.
- Owner: `src/runtime`; the fix preserves the ECS geometry source as the CPU authority and keeps GPU refresh deferred through the existing dirty/extraction path.

## Required changes
- [x] Add a focused runtime repro that recomputes normals before a pending direct mesh post-process applies.
- [x] Preserve count-matched current mesh `v:normal` values across direct mesh post-process re-population.
- [x] Keep generated normal texture/material binding registration intact for data plumbing.
- [x] Continue marking direct mesh geometry dirty after post-process apply.

## Tests
- [x] Add regression coverage under the runtime contract suite.
- [x] Run the regression once before the fix and confirm it fails with the old imported normals.
- [x] Run adjacent normal recompute, direct import, dirty extraction, and shader contract tests after the fix.

## Docs
- [x] Record the post-process overwrite policy in this bug task.
- [x] Update runtime docs for direct mesh post-process normal preservation.

## Acceptance criteria
- [x] Repro is documented and reliably covered by automated test(s).
- [x] Direct mesh post-process still registers generated normal textures when available.
- [x] A count-matched editor-authored `v:normal` vector survives the post-process apply.
- [x] Fix does not introduce layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi.MeshVertexNormalsCommandSurvivesPendingDirectMeshPostProcess' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi.MeshVertexNormalsCommand(PublishesCanonicalNormalsForAllWeightings|SurvivesPendingDirectMeshPostProcess|FailsClosedForInvalidTargets)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'AssetImportFormatCoverage.DirectObjImport(BakesGeneratedNormalTextureFromAuthoredVertexNormals|ComputesAndBakesGeneratedNormalTextureWhenMissingNormals|ComputesVertexNormalsWhenMissing)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'MeshGeometryExtraction.*DirtyVertexAttributes|RendererFrameLifecycle.ForwardSurfacePipelineSurvivesOperationalRebuild' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Shipping a fix without a regression test when one is feasible.
- Mixing mechanical file moves with semantic refactors.
- Reintroducing fragment-shader normal texture sampling for current surface shading.

## Maturity
- Target: `CPUContracted`.
- The regression exercises the real runtime `Engine::Run()` and `StreamingExecutor` apply path under the CPU/null gate. No `Operational` follow-up is owed for this race because GPU refresh remains owned by the existing dirty extraction/upload contracts and the shader-normal policy is tracked by `BUG-047`.
