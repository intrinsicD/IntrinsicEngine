---
id: ASSETIO-008
theme: none
depends_on: [GEOM-025, ASSETIO-006, ASSETIO-007]
maturity_target: CPUContracted
---
# ASSETIO-008 — Default UV atlas materialization for imported meshes

## Goal
- Ensure runtime asset materialization precomputes valid texture coordinates for imported renderable meshes by preserving valid authored UVs or invoking the default UV atlas backend when UVs are missing or invalid.

## Non-goals
- No new geometry backend implementation beyond consuming `GEOM-025`.
- No renderer pass, shader, Vulkan, or UI changes.
- No persistent on-disk atlas sidecar format.
- No replacement of authored UVs by default when they pass validation.
- No broad asset format parser rewrite.

## Context
- Status: done (retired 2026-06-16; CPUContracted).
- Owner/agent: Codex.
- Branch / PR: current branch / local commit.
- Next verification step: retired; preserve the ASSETIO-008 focused verification block below for regression triage.
- Owning subsystem/layer: runtime-owned asset materialization and import handoff. `assets` remains CPU-only and GPU-agnostic; renderer/GPU residency remains downstream.
- Current generated normal and color texture bakes require `v:texcoord` and fail closed when UVs are missing.
- Direct mesh imports and model-scene imports already preserve decoded `v:texcoord` when present and preserve or compute `v:normal`.
- The new policy is: imported renderable meshes should enter ECS/render materialization with resolved UVs before generated normal, albedo, scalar, or vector texture bakes run.
- The fallback may seam-split the renderable mesh. Runtime must preserve copied vertex properties through source-vertex xrefs and keep diagnostics/provenance visible.

## Required changes
- [x] Add a runtime mesh UV resolution helper that consumes `Geometry::MeshIO::MeshIOResult` or `Geometry::HalfedgeMesh::Mesh`, validates authored UVs through the `GEOM-025` contract, and invokes the selected backend when UVs are missing or invalid.
- [x] Add materialization options for backend selection, preserve-authored-vs-force-regenerate policy, atlas resolution, padding, texels-per-unit, and failure behavior.
- [x] Run UV resolution before generated normal/albedo texture bakes in model-scene handoff.
- [x] Run UV resolution before direct mesh generated normal texture binding and ECS population.
- [x] Preserve normals, colors, scalar fields, vector fields, and source-index provenance across seam-split output meshes.
- [x] Record diagnostics for authored UV preserved, generated UV, invalid authored UV, backend failure, seam-split vertex count, chart count, and atlas dimensions.
- [x] Keep imports fail-closed only when the mesh is renderable but UV resolution cannot produce valid texture coordinates under the selected required policy.
- [x] Keep authored material textures higher priority than generated fallback textures; UV generation only supplies the mapping needed to sample or bake them.

## Tests
- [x] Add direct mesh import coverage proving an OBJ/STL-style mesh without UVs materializes with finite `v:texcoord`.
- [x] Add model-scene coverage proving a glTF primitive without `TEXCOORD_0` receives generated UVs before generated texture bakes.
- [x] Add coverage proving valid authored UVs are preserved by default and do not invoke the fallback backend.
- [x] Add coverage proving invalid authored UVs can trigger fallback generation with diagnostics.
- [x] Add coverage proving generated normal and generated albedo bakes succeed for a mesh that originally had no authored UVs but receives generated UVs.
- [x] Add seam-split property propagation coverage for `v:normal`, `v:color`, and a scalar property.
- [x] Add fail-closed coverage for backend failure under required-UV policy.

## Docs
- [x] Update `src/runtime/README.md` with the resolved-UV import/materialization policy and provenance diagnostics.
- [x] Update `tasks/backlog/assets/README.md` if follow-up import work is opened.
- [x] Update `docs/architecture/parameterization-mapping-roadmap.md` or a runtime architecture doc if the final materialization policy changes.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module surface changes.

## Acceptance criteria
- [x] Imported renderable mesh assets have valid `v:texcoord` after runtime materialization, either authored or generated.
- [x] `xatlas` is used by default through the geometry backend seam when UVs must be generated.
- [x] The backend can be replaced through runtime options for tests and future algorithms.
- [x] Generated normal/albedo texture bakes no longer skip solely because the source asset omitted UV coordinates.
- [x] Runtime diagnostics distinguish authored UVs from generated UVs and record backend failures without involving graphics or Vulkan ownership.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicAssetUnitTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeAssetModelSceneHandoff|RuntimeAssetImportFormatCoverage|RuntimeMeshAttributeTextureBake|UvAtlas' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not move `AssetService`, generated texture asset registration, or texture upload ownership below `runtime`.
- Do not add live graphics handles or backend-specific state to ECS components.
- Do not silently write all-zero UVs on backend failure.
- Do not make `assets` import `geometry`, `runtime`, `graphics`, or `xatlas`.
- Do not replace authored texture coordinates unless the user/runtime option explicitly requests regeneration or validation rejects them.

## Maturity
- Target: `CPUContracted`.
- This task closes runtime materialization of resolved UVs. `Operational` owned by `GRAPHICS-088`.
- Retired at `CPUContracted` with runtime-owned resolved-UV materialization,
  direct/model generated texture bakes using generated atlases, replaceable
  backend coverage, seam-split property propagation, and fail-closed backend
  failure coverage.
