---
id: BUG-044
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-044 — Runtime mesh import blocks on derived post-processing

## Goal
- Make direct runtime mesh imports publish decoded geometry before expensive derived mesh work, then queue missing-normal, UV-atlas, and generated-texture work as downstream tasks that update the same entity when ready.

## Non-goals
- No renderer-side UV or normal fabrication.
- No graphics/Vulkan operational proof.
- No new generic editor task-graph UI for every derived operation.
- No broad model-scene material-handoff rewrite in this slice unless it falls out of the direct mesh queue seam without extra state ownership.
- No changes to geometry atlas quality or xatlas backend policy.

## Context
- Symptom: object loading can take extremely long after ASSETIO-008 because direct mesh import decodes, computes fallback normals, resolves/generates UV atlases, converts the resolved mesh, and bakes generated normal textures before reporting import completion.
- Expected behavior: geometry should load first; successive downstream tasks such as missing-normal computation, UV atlas resolution, and generated texture baking should be queued and applied when ready.
- Impact: large no-UV meshes can stall the import/apply path and make the sandbox feel blocked even though raw geometry was already decoded.
- Owning subsystem/layer: `runtime` import/materialization. `geometry` remains the atlas backend owner; `assets` remains CPU payload authority; graphics/RHI see only updated runtime snapshots.

## Completion
- Completed: 2026-06-16. Commit/PR: this local fix commit.
- Root cause: ASSETIO-008 moved the direct mesh import path onto the full runtime materialization path before import completion, so decode/import also ran fallback normal synthesis, UV atlas resolution, halfedge conversion, and generated normal texture baking.
- Fix summary: direct mesh import now publishes a raw decoded mesh entity first, then queues the full materialization/bake path on `Runtime.StreamingExecutor`. The main-thread apply replaces `GeometrySources` on the same entity, stamps geometry dirty tags, and registers the generated normal material binding only after the deferred result is ready.

## Required changes
- [x] Add or reuse a runtime helper that materializes raw decoded mesh geometry without invoking UV atlas resolution or generated texture baking.
- [x] Change direct mesh import to create the entity from raw geometry and return import completion before generated texture work is finished.
- [x] Queue derived mesh post-processing on `Runtime.StreamingExecutor` and apply results on the main thread.
- [x] Apply downstream results to the same entity, preserving stable identity, selection, render components, and generated material texture binding.
- [x] Stamp geometry dirty tags when the deferred result replaces mesh `GeometrySources`.

## Tests
- [x] Add regression coverage proving direct OBJ import without authored UVs returns after geometry publication with no generated texture yet.
- [x] Add regression coverage proving a later engine frame applies finite resolved UVs and generated normal texture binding to the same entity.
- [x] Keep existing direct import authored-normal, missing-normal, generated-UV, model-scene, and dropped-file coverage passing.

## Docs
- [x] Update `src/runtime/README.md` with the direct mesh import queue split.
- [x] Update bug backlog state and regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [x] Direct mesh import reports `PrimitiveEntitiesCreated == 1` before generated texture counts are non-zero.
- [x] The imported mesh entity exists immediately after import and keeps the same entity handle after deferred post-processing applies.
- [x] Missing normals and missing/invalid UVs still resolve through the ASSETIO-008 materialization path before generated texture bakes.
- [x] Deferred post-processing failures do not invalidate the already-published raw geometry import.
- [x] The fix does not introduce layering violations.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
# Passed.

ctest --test-dir build/ci --output-on-failure -R 'RuntimeAssetImportFormatCoverage\.DirectObjImport' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 4/4 tests.

ctest --test-dir build/ci --output-on-failure -R 'RuntimeAssetImportFormatCoverage|SandboxEditorUi.*Dropped|SandboxEditorUi\.EngineImportFacadeMaterializes(NonManifoldObj|ObjWithoutAuthoredTexcoords)AsRenderableMesh|RuntimeAssetMeshNormals|RuntimeAssetModelSceneHandoff' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 29/29 tests.

python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
# Passed.

cmake --build --preset ci --target IntrinsicTests
# Passed.

ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 3069/3069 tests.

python3 tools/agents/generate_session_brief.py --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
# Passed.
```

## Forbidden changes
- Do not move atlas generation into `assets`, `graphics`, ECS, or UI.
- Do not let lower layers import runtime or graphics.
- Do not make render extraction accept missing/invalid surface texcoords.
- Do not create duplicate ECS entities for post-processed versions of the same import.

## Maturity
- Target: `CPUContracted`.
- This slice proves the runtime CPU/null import contract. `Operational` generated-UV Vulkan sampling remains owned by `GRAPHICS-089`.
