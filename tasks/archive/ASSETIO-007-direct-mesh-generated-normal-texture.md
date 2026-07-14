---
id: ASSETIO-007
theme: F
depends_on: [ASSETIO-006, BUG-041, RUNTIME-085]
maturity_target: CPUContracted
---
# ASSETIO-007 — Direct mesh generated normal texture binding

## Goal
- Make direct mesh asset imports use the same default normal policy as model-scene imports: preserve authored vertex normals when present, compute area-weighted normals when absent, bake bakeable CPU `v:normal` data into a generated normal texture, upload it, and bind it to the imported mesh surface material for shading.

## Non-goals
- No new UV unwrapping or parameterization algorithm; generated normal textures remain gated on existing `v:texcoord`.
- No editor UI for choosing normal-map source properties.
- No GPU/Vulkan readback proof in this slice.
- No changes to model-scene authored normal texture priority.

## Context
- Owner: `runtime`; direct file imports are composed by `Extrinsic.Runtime.Engine`.
- `BUG-041` already guarantees imported OBJ-style `vn` data becomes CPU mesh-domain `v:normal`, with area-weighted fallback when source normals are absent.
- `ASSETIO-006` added `MeshAttributeTextureBake` and model-scene generated normal texture handoff, but direct `AssetPayloadKind::Mesh` imports still materialize with the extraction-owned default `StandardPBR` sidecar and no generated normal texture binding.
- The extraction cache owns renderable material leases, so direct imports should pass data-only `MaterialTextureAssetBindings` keyed by stable render id instead of storing graphics handles in ECS.

## Completion
- Completed: 2026-06-13. Commit/PR: this retirement commit.
- Fix summary: direct mesh imports now reuse the shared runtime normal contract, bake a generated normal texture from CPU `v:normal` when the mesh also has matching `v:texcoord`, load the generated texture as a child asset, request upload when the active backend accepts texture uploads, and register an extraction-cache `MaterialTextureAssetBindings` record keyed by stable render id. Extraction resolves that data-only binding onto the sidecar-owned material lease during `ExtractAndSubmit`.
- UV caveat: direct mesh imports without bakeable texture coordinates still succeed and still expose preserved/computed CPU `v:normal`; only the generated texture binding is skipped.

## Required changes
- [x] Add a runtime extraction-cache material texture binding surface keyed by stable render id.
- [x] During direct mesh import, bake a generated normal texture from `v:normal` when `v:texcoord` is present, load it as a generated texture asset, request GPU upload, and register the normal binding with render extraction.
- [x] Keep direct mesh import successful when normal texture baking is not representable, recording no binding rather than failing the import.
- [x] Make runtime import result/logging expose generated texture asset and upload counts.

## Tests
- [x] Add direct OBJ import coverage proving `v/vt/vn` imports create a generated normal texture asset, upload request, CPU `v:normal`, and extraction material binding.
- [x] Add direct OBJ import coverage proving missing `vn` computes area-weighted CPU normals and can still generate a normal texture when `vt` exists.
- [x] Add render-extraction coverage proving cached material texture bindings resolve onto the extraction-owned material sidecar when the GPU cache has a ready texture/fallback view.

## Docs
- [x] Update runtime docs for direct mesh generated normal texture defaults and UV-gated bake behavior.
- [x] Retire this task and update the retirement log after verification.

## Acceptance criteria
- [x] Direct mesh imports with `v:texcoord` and either authored or computed `v:normal` produce a generated normal texture binding by default.
- [x] Direct mesh imports without bakeable texture coordinates still preserve/compute CPU `v:normal` and render through the existing material fallback without import failure.
- [x] Generated normal texture bindings stay data-only across ECS/runtime/extraction boundaries.
- [x] Focused runtime tests pass under the CPU/null gate.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
# Passed.

ctest --test-dir build/ci --output-on-failure -R 'RuntimeAssetImportFormatCoverage|MeshGeometryExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 26/26 tests.

python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
# Passed: wrote docs/api/generated/module_inventory.md (484 modules).

cmake --build --preset ci --target IntrinsicTests
# Passed.

ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 3011/3011 tests.

python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
git diff --check
# Passed. Root hygiene remains warning-mode with the existing imgui.ini finding.
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding graphics handles or live GPU ownership to ECS components.
- Treating missing `v:texcoord` as a fatal direct mesh import error.

## Maturity
- Target: `CPUContracted`.
- No `Operational` follow-up is owed for this direct import binding contract; broader GPU/Vulkan visual readback remains value-gated by future rendering acceptance work.
