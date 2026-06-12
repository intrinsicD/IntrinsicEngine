---
id: BUG-041
theme: G
depends_on: [GEOIO-002S, RUNTIME-085, ASSETIO-001]
maturity_target: CPUContracted
---
# BUG-041 — Asset mesh vertex normals are lost during runtime materialization

## Goal
- Preserve vertex normals from mesh asset payloads into promoted runtime `GeometrySources`, and synthesize deterministic vertex normals for loaded mesh assets that do not provide them.

## Non-goals
- No new file-format parser rewrite; geometry IO remains the source of decoded mesh payloads.
- No Vulkan-only shader or GPU-device feature work.
- No tangent/bitangent generation, hard-edge seam splitting, or smoothing-group interpretation.
- No asset import queue/progress UI work; that remains tracked by `ASSETIO-005`.

## Context
- Owner/layer: `runtime` materializes decoded mesh assets into `GeometrySources`; `geometry` owns decoded CPU mesh payloads and halfedge mesh structures; `graphics` consumes runtime-packed geometry snapshots.
- Geometry IO already has OBJ vertex-normal importer coverage (`GEOIO-002S`), and glTF model-scene payload construction writes `v:normal` when the source attribute exists.
- The runtime direct mesh path (`Runtime.Engine::BuildHalfedgeMesh`) and model-scene handoff path (`Runtime.AssetModelSceneHandoff::BuildHalfedgeMesh`) rebuild halfedge meshes from `v:point` and `f:vertices` only, dropping `v:normal` before `PopulateFromMesh` copies mesh vertex properties into ECS.
- Missing source normals currently remain absent after load, so downstream visualization/property workflows cannot rely on a canonical `v:normal` for mesh assets.

## Completion
- Completed: 2026-06-12. Commit/PR: this retirement commit.
- Root cause: promoted geometry/model decoders already produced one `v:normal` vector per source vertex when the file format supplied normals, but runtime materialization rebuilt halfedge meshes from positions and face indices only. Direct mesh import and model-scene handoff therefore discarded decoded normals before ECS `GeometrySources` population, and the mesh surface packer then wrote zero payload values where the retained surface vertex format can carry an encoded normal.
- Fix summary: shared runtime mesh materialization now copies explicit per-vertex normals, computes normalized area-weighted fallback normals for loaded meshes without source normals, applies the same path to direct mesh imports and model-scene primitive materialization, and packs available mesh normals into the existing 20-byte surface vertex layout's U/V fields with octahedral encoding.

## Required changes
- [x] Add shared runtime-side normal materialization logic that copies explicit `v:normal` when the decoded mesh payload provides one vector per vertex.
- [x] Compute area-weighted fallback vertex normals from loaded mesh positions/faces when explicit normals are absent.
- [x] Apply the same normal materialization in direct mesh imports and model-scene primitive materialization.
- [x] Ensure runtime mesh packing preserves the existing packed-vertex contract while carrying mesh normals through the available normal/UV slot where applicable.
- [x] Remove any temporary diagnosis probes.

## Tests
- [x] Add a direct runtime mesh import regression proving OBJ `vn` data survives as mesh-domain `GeometrySources` `v:normal`.
- [x] Add a no-source-normal regression proving runtime materialization computes normalized fallback vertex normals.
- [x] Add/update mesh packer coverage proving explicit mesh normals are encoded into packed vertex data instead of being zeroed.
- [x] Add/update model-scene handoff coverage proving model-scene primitive normals survive materialization.

## Docs
- [x] Update `tasks/backlog/bugs/index.md`, retire the task record after the fix, append the retirement narrative, and regenerate `tasks/SESSION-BRIEF.md`.
- [x] Update runtime documentation if the mesh packer/materialization contract changes.
- [x] Refresh generated module inventory only if a module surface changes.

## Acceptance criteria
- [x] Mesh asset imports with vertex normals expose `v:normal` on the resulting mesh-domain `GeometrySources` vertex source.
- [x] Mesh asset imports without vertex normals expose computed unit-length `v:normal` values for non-degenerate surface vertices.
- [x] Model-scene materialization preserves or computes the same mesh vertex-normal property.
- [x] Mesh surface packing no longer discards available mesh normals when producing packed vertex bytes.
- [x] Focused runtime contract tests pass under the CPU gate.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
# Passed.

ctest --test-dir build/ci --output-on-failure -R 'RuntimeAssetImportFormatCoverage|RuntimeAssetModelSceneHandoff|MeshGeometryPacker' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 34/34 tests.

python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
# Passed: wrote docs/api/generated/module_inventory.md (483 modules).

cmake --build --preset ci --target IntrinsicTests
# Passed.

ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 2999/2999 tests.

python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
# Passed.
```

## Forbidden changes
- Moving asset IO ownership into graphics or Vulkan.
- Inventing a new live asset-service dependency below `runtime`.
- Changing lower-layer geometry IO parser behavior without focused geometry IO tests.
- Treating synthesized normals as a substitute for source-format hard-edge or smoothing-group support.

## Maturity
- Target: `CPUContracted`; no `Operational` Vulkan follow-up is owed by this task because it fixes CPU asset materialization and runtime packing contracts. Future shader-side normal visualization changes should open a separate rendering task.
