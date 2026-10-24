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
- [x] Regression first: a seam-bearing model-scene import that must request the normal and
      albedo bakes fails before the fix, and the failure is recorded.
- [x] The fix is made once at the canonical owner of the bake gate. Vertex-UV and no-UV
      primitives keep their current behaviour, and the existing direct-mesh bake test still
      passes.
- [x] No unrelated changes.

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

## Resolution — 2026-10-24
- **Reproduced, then fixed** (line numbers at `b353492b8`). The direct normal-bake request (`:1272`) and
  albedo-bake request (`:1368`) in `QueueProgressiveEnrichmentJobs`
  (`Runtime.AssetWorkflowModelMaterialization.cpp`) were gated on `MeshHasVertexTexcoords`. That helper
  checks only `v:texcoord`. A seam atlas leaves `h:texcoord` as the only UV channel, and so do
  authored corner UVs with a seam, so both bakes were skipped. Production always takes this direct
  branch, because the module's materializer never sets `ProgressiveJobs`.
- **Change.** Both gates now read `PreparedPrimitive::HasResolvedTexcoords`. That flag comes from
  `HasValidTexcoords`, which checks the corner domain first and then that the values are finite.
  The model path's own slot recipe and the direct-mesh path already use it. No new helper, no
  interface change. `MeshHasVertexTexcoords` remains only in the progressive branch (`:1153`,
  left to BUG-097). Bake stale and failure handling is unchanged.
- **Regression test.**
  `RuntimeAssetImportFormatCoverage.ModelSceneSeamAtlasRequestsGeneratedTextureBakes`
  (IntrinsicRuntimeContractTests). It uses a hand-built `AssetModelScenePayload` with one material
  without textures and four `v:color` primitives, each loaded through `LoadOBJ`:
  - a closed cube with a generated seam atlas (`ClosedCubeObjText`);
  - a triangle with payload `v:texcoord` (vertex-UV control);
  - the under-resolved 6-vertex fixture with a rejected atlas (no-UV control);
  - the authored corner-seam quad, now the shared `AuthoredCornerSeamQuadObjText` helper.

  It loads the payload through `AssetService::Load` and `CompleteCpuLoadAndFlushEvent`. Fatal
  preconditions pin each primitive's UV domain. The predictions were written as assertions before
  the first run:
  - 6 bake requests;
  - for the seam, vertex-UV and corner-seam primitives, the "non-operational; no CPU fallback"
    diagnostic in both the Normal and Albedo slots;
  - for the no-UV primitive, the two "waiting" diagnostics.
- **Verification run this session.** Raw logs are in `tasks/evidence/BUG-250/`.
  - **Before the fix:** FAIL (`regression-before-fix.log`). `BakeRequests` was 2, expected 6. The
    seam-atlas and authored-corner-seam primitives showed the "waiting for resolved UVs…" (Normal)
    and "waiting for vertex color albedo bake" (Albedo) diagnostics. All preconditions held, and the
    vertex-UV and no-UV assertions passed.
  - **After the fix:** PASS (`regression-after-fix.log`).
  - **Mutation, both gates forced to `true`:** FAIL as intended (`mutation-always-gate.log`). The
    no-UV Normal and Albedo diagnostics were wrong, and `BakeRequests` was 8 instead of 6. The fix
    was restored afterwards, and `git diff src` shows only the two-token change.
  - **Owning suites:** `RuntimeAssetImportFormatCoverage` (52), `AssetWorkflowModule` (16) and
    `RuntimeTextureBakeModule` (32), with `-LE 'gpu|vulkan|slow|flaky-quarantine'`: 100/100 passed
    (`suites-after-fix.log`). This includes
    `DirectObjImportQueuesGeneratedNormalBakeFromAuthoredVertexNormals`,
    `DirectObjImportPreservesAuthoredCornerUvs` and
    `CanonicalBindingSharesCornerAtlasAndRecordsSourceIdentity`.
  - **Repository checks:** `check_layering --strict`: no violations. `check_test_layout --strict`:
    findings=0.
  - **Duplication:** jscpd (default settings) over `tests/contract/runtime` and
    `src/runtime/AssetWorkflow` found no clone touching the new test, the helper or the gates. One
    clone touches the refactored caller; it is the shared BUG-137 engine setup and already exists at
    `b353492b8`.
- **Chosen defaults.**
  - The gate reads `HasResolvedTexcoords`, not a new helper.
  - A hand-built payload instead of a glTF file, because glTF import keeps only
    POSITION/NORMAL/TEXCOORD_0 (`Runtime.AssetWorkflowModelTextureDecode.cpp:876-920`). With no
    COLOR_0, the albedo half cannot be reached from a glTF file, and glTF never yields authored
    corner UVs.
  - The no-UV control pins both call sites through their diagnostics.
- **Deliberate behaviour change, for the AC2 decision.** A primitive whose `v:texcoord` is present
  but non-finite no longer gets a bake request. This follows the `HasValidTexcoords` rule and the
  import-visibility contract ("leaves texture baking unavailable until an accepted atlas exists").
  Valid vertex-UV primitives and no-UV primitives behave as before (controls b and c).
- **Operational smoke:** none owed. The defect was the CPU-side request gate, which the contract
  test observes directly. The consumer's `h:texcoord` binding is covered by
  `RuntimeTextureBakeModule.CanonicalBindingSharesCornerAtlasAndRecordsSourceIdentity`. The test
  proves that the request is made, not that a GPU bake of a seam atlas succeeds.
- **Remains:**
  - An independent correctness review of the change.
  - A ponytail review.
  - The operator decision on COLOR_0 import (see the lane report).

## Completion — 2026-10-24
Completed 2026-10-24. Commit: `b353492b8` (filing, with the regression test's name fixed
before the fix), `9c5b4559e` (the fix and its test, on `b353492b8`, branch
`claude/bug-250-seam-atlas-bake-gate`), merged into the night integration branch
`claude/night-2026-10-24-bug-sweep` as `de1fbb832`. Not yet on `main`; hosted CI has not run on
these commits, and nothing was built or run at the merge.

Reviewer decisions (coordinator's local review record `wfI.json`, lane `BUG-250`, outside the
repository), round 1 on `9c5b4559ea10f18633b2c0237337eb22a51ba07c` (`b353492b8..9c5b4559e`):
- Independent Claude review `approve`, no blocking findings; AC1, AC2 and AC3 "met". It accepts
  the non-finite `v:texcoord` change within AC2. It reran four `RuntimeAssetImportFormatCoverage`
  cases (4/4 passed) and reports the three owning suites at 100/100 on the existing build tree.
- Independent Codex review (thread `01a16c1c-e79a-7a82-a1ec-11350060fc6b`) `approve`, no
  blocking findings, ending "VERDICT: approve". Its table rates all three criteria "**Met**".
  It ran no builds or tests.

Boxes: all three are ticked, as both round-1 reviews decided.

Limits:
- The test proves that the CPU-side bake request is made, not that a GPU bake of a model-scene
  seam atlas succeeds. No `ci-vulkan` smoke was run.
- In the `ProgressiveRawGeometryFirst` branch (`:538-540`), `HasResolvedTexcoords` is valid
  `v:texcoord` or an `h:texcoord` existence check. No production composition sets that option,
  and the progressive path belongs to BUG-097.
- The rejected-atlas OBJ text in the test is still inline twice (`:2095`, `:2223`).
- Context and `evidence_skip_reason` are the filing record ("not reproduced"); the Resolution
  records the reproduction.
- The duplication check covered `tests/contract/runtime` and `src/runtime/AssetWorkflow`, not
  the whole repository.

Open for the operator:
- The deliberate behaviour change: a primitive whose `v:texcoord` is present but non-finite no
  longer gets a bake request. Both reviewers accept it within AC2.
- COLOR_0 import: the glTF decoder imports only POSITION, NORMAL and TEXCOORD_0, so the
  model-scene albedo bake is unreachable from glTF files. Nothing was filed.
- Whether an Operational `ci-vulkan` seam-atlas bake smoke is wanted.

Evidence: the logs cited in the Resolution stay git-ignored in the `bug-220` lane worktree
(`tasks/evidence/BUG-250/`); the integration worktree has none. Nothing was deleted.
