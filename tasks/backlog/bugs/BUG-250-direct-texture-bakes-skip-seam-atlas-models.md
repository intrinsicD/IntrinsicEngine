---
id: BUG-250
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: found by code reading during the BUG-097 night lane and not reproduced; the regression test required below is the first evidence
contract_schema: 1
contracts: [geometry.element-domain-sources]
---
# BUG-250 — Direct texture bakes skip models with a seam atlas

## Goal
- Model-scene primitives whose resolved UVs live only on the corner domain (`h:texcoord`)
  get the same automatic normal and albedo bake requests as primitives with vertex UVs
  (`v:texcoord`).

## Context
- Found by code reading during BUG-097 work on 2026-10-24 (the BUG-097 lane's "Finding for the
  09:00 decision", branch `claude/bug-097-remove-progressive-model-path`, not merged). **Not
  reproduced**: no test or run has shown the skipped request yet. Line numbers are at
  `dbcf162cd`.
- `Runtime.AssetWorkflowModelMaterialization.cpp`: `MeshHasVertexTexcoords` (`:934`) checks only
  `v:texcoord`. It gates the direct (non-progressive) normal-bake request (`:1272`) and the
  direct albedo-bake request (`:1368`). When the gate fails, the normal slot records "waiting for
  resolved UVs and vertex normals before property texture bake request".
- `Runtime.AssetWorkflowGeometryMaterialization.cpp`: when the generated atlas has a seam,
  `PublishCornerTexcoords` (`:586`) publishes corner UVs and removes `v:texcoord`, so
  `h:texcoord` is the only UV authority; its comment (`:581-584`) says so.
  `BuildRuntimeHalfedgeMeshMaterialization` (`:795`), which model-scene materialization calls,
  takes that branch when `corners->HasSeam` (`:944`).
- So a model whose generated atlas has seams likely never gets the automatic bake request.
  Primitives with authored corner UVs (`:527-528`) probably hit the same gate; also not checked.
- The bake consumer is not the limit: `TextureBakeModule` resolves a complete corner
  `h:texcoord` or a vertex `v:texcoord` (`Runtime.TextureBakeModule.cpp:415-416`, `:528-573`).
  `docs/architecture/property-coherence.md:437-441` says that watching only `v:texcoord` is
  wrong for a seam-carrying mesh.
- Candidate fix, not decided: gate on `PreparedPrimitive::HasResolvedTexcoords` (`:72`), which
  is already computed: from authored UVs (`:540`) or from
  `Diagnostics.ResolvedTexcoordsValid` (`:587`), whose `HasValidTexcoords` reads the corner
  domain first (`Runtime.AssetWorkflowGeometryMaterialization.cpp:257`).
- The progressive branch has the same `v:texcoord` check (`:1153`). The BUG-097 lane proposes
  removing that path, so it is out of scope here.
- Owning tests: `tests/contract/runtime/Test.AssetImportFormatCoverage.cpp`
  (`RuntimeAssetImportFormatCoverage.*`) in `IntrinsicRuntimeContractTests`, a CPU-only
  contract executable (`tests/CMakeLists.txt`, `_runtime_contract_test_files`; the
  `build/ci` registration lists `RuntimeAssetImportFormatCoverage.RejectedAtlasDoesNotDiscardModelScene`
  there). According to the BUG-097 lane, no CPU test targets the model-scene direct normal-bake
  branch.

## Acceptance criteria
- [ ] Regression first: a seam-bearing model-scene import that must request the normal and
      albedo bakes fails before the fix, and the failure is recorded.
- [ ] The fix is made once at the canonical owner of the bake gate. Vertex-UV and no-UV
      primitives keep their current behaviour, and the existing direct-mesh bake test still
      passes.
- [ ] No unrelated changes.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeAssetImportFormatCoverage\.ModelSceneSeamAtlasRequestsGeneratedTextureBakes$' \
  --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeAssetImportFormatCoverage\.DirectObjImportQueuesGeneratedNormalBakeFromAuthoredVertexNormals$' \
  --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -R '^RuntimeAssetImportFormatCoverage\.' \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```
