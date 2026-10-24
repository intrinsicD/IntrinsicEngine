---
id: BUG-097
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
maturity_target: CPUContracted
contract_schema: 1
contracts:
  - geometry.element-domain-sources
  - geometry.property-coherence
---
# BUG-097 — Progressive model-scene UV job publishes a zero atlas

## Goal
- Make the default-off progressive model-scene UV job publish a real,
  generation-valid atlas or fail closed, while guaranteeing that a stale
  completion cannot overwrite user-authored UVs or newer geometry.

## Operator decision and start gate (2026-09-05)

- Atlas construction is being replaced. The intended creation policy is that
  every newly created surface-mesh asset receives an atlas when UVs are
  missing, through one improved shared method. This covers imported/model
  primitives and procedural surface creation, preserves valid authored UVs,
  and does not require atlases for non-surface assets. Atlas completion may be
  asynchronous; raw visibility is not a claim of UV/bake readiness.
- BUG-097 is an interim correctness task only, not another atlas algorithm
  project. Before implementing, identify the operator's replacement task/branch
  and reproduce a currently needed workflow that still reaches this defect.
  If the replacement removes the affected path before it is needed, verify
  that replacement and retire BUG-097 by supersession instead of repairing
  the obsolete implementation. Do not close merely because replacement work
  is planned.
- Current source inspection still finds `WriteDefaultTexcoords` in
  `src/runtime/AssetWorkflow/Runtime.AssetWorkflowModelMaterialization.cpp`;
  `AssetWorkflowModelMaterializationOptions::ProgressiveRawGeometryFirst`
  remains false by default. No current affected-workflow reproduction was
  established in this documentation session, so no interim code repair is
  authorized by this note alone.
- `METHOD-040` and `GEOM-076` are related existing research/adoption work, not
  a confirmed identity for the operator's in-progress replacement. Do not
  silently select them, alter their gates, or start unattended implementation
  before the replacement owner and interim necessity are resolved.

## Non-goals
- No enabling `ProgressiveRawGeometryFirst` by default.
- No zero-filled authoritative `v:texcoord` placeholder.
- No synchronous UV generation on the initial raw-publication path.
- No new UV-unwrapping method when the existing runtime halfedge
  materialization/atlas seam can satisfy the contract.
- No duplicate replacement method or automatic promotion of the progressive
  option. The eventual creation policy belongs to the confirmed shared-atlas
  replacement/integration owner, not a default toggle in this bug fix.
- No GPU texture-bake implementation or unrelated progressive-job redesign.

## Context
- Owner: runtime progressive model-scene materialization and derived-job
  generation validation. Geometry owns the CPU atlas method; runtime owns ECS
  snapshots, scheduling, dependency state, publication, and stale discard.
- `WriteDefaultTexcoords()` currently fills every `v:texcoord` with `(0, 0)`.
  The progressive job is named `generate mesh uv atlas` but calls that helper,
  so a completed job claims an atlas while publishing a degenerate
  authoritative property.
- The current no-op helper carries a constant payload token and its apply
  lambda checks only entity validity, not source revisions or binding
  identity. A user can parameterize UVs, edit topology or
  positions, or replace bindings before completion and then lose that newer
  work to the zero-filled result.
- The retired regression
  `RuntimeAssetModelSceneHandoff.ProgressiveRawGeometryFirstPublishesNormalsAndQueuesUvAndBakeJobs`
  asserted only that `v:texcoord` eventually existed. RUNTIME-200 removed its
  handoff test file; add any needed regression through the current
  `AssetWorkflowModule`/staged-import owner instead of restoring that surface.
- The progressive option is currently default-off in its materialization options, so this
  is a latent opt-in correctness defect rather than evidence that the viewer's
  default model path runs progressive enrichment.
- Archived progressive-enrichment work requires real async atlas output and
  generation-safe derived publication. Archived UV-fallback guidance allows
  renderer-local zero UVs for raw packing only; it explicitly does not justify
  writing zero UVs into authoritative ECS geometry or starting bake work that
  requires a real atlas.

## Required changes
- [ ] Satisfy the start gate and record the necessary interim repair versus
      verified replacement/supersession disposition before changing code.
      The repair checklist below applies only to the interim-repair branch.
- [ ] Replace the no-op job/zero-fill publication with the existing CPU halfedge
      materialization/UV-atlas path over an immutable snapshot of positions,
      topology, and relevant source properties.
- [ ] Return an explicit success/failure payload containing finite,
      count-matched atlas coordinates and diagnostics/provenance sufficient to
      distinguish real atlas output from a renderer fallback.
- [ ] Use canonical corner-over-vertex UV resolution. Publish seam UVs as
      `h:texcoord` mapped back to original source faces/halfedges; keep source
      topology and every element count unchanged. Remove a superseded UV
      authority, not unrelated properties. Unwrapper split vertices are
      intermediate/GPU-upload data, never replacement ECS topology.
- [ ] Capture authoritative entity, geometry, source-property, and binding
      generations at submission and validate all of them on the main thread
      immediately before publication.
- [ ] On any generation mismatch, report stale discard and perform no geometry
      or property write. In particular, preserve exact UVs authored after
      submission and never merge the older atlas into changed topology.
- [ ] On atlas failure, leave authoritative UVs absent (or preserve a
      pre-existing valid property), report the failure, and keep dependent
      texture-bake jobs blocked/failed rather than marking the UV dependency
      ready.
- [ ] Preserve non-UV source properties and initial raw publication. Worker
      computation stays off the main thread; validated publication stays on
      the main thread.

## Tests
- [ ] Regression first through the current staged import seam: add
      `AssetWorkflowModule.ProgressiveUvAtlasPublishesCornerUvsWithoutTopologyChange`
      in `tests/contract/runtime/Test.AssetWorkflowModule.cpp`. Assert canonical
      UV resolution, count/finiteness, real atlas diagnostics, non-degenerate
      mapped triangles, a seam-bearing fixture, and unchanged source topology.
- [ ] Add
      `AssetWorkflowModule.ProgressiveUvAtlasDiscardsCompletionAfterUserUvEdit`.
      Hold completion deterministically, write distinctive user UVs, release
      it, and assert exact preservation plus a stale-discard status.
- [ ] Add
      `AssetWorkflowModule.ProgressiveUvAtlasDiscardsCompletionAfterTopologyEdit`
      and assert no old atlas is applied to the newer vertex/topology
      generation.
- [ ] Add
      `AssetWorkflowModule.ProgressiveUvAtlasFailureLeavesBakeDependencyBlocked`
      with a deterministic atlas failure fixture. Assert no zero-filled
      authoritative property, terminal failure diagnostics, and no dependent
      bake publication.
- [ ] Retain coverage that the worker executes off the main thread, successful
      publication executes on the main thread exactly once, and initial raw
      geometry is available before enrichment resolves.

## Docs
- [ ] Correct the runtime progressive-import documentation to distinguish raw
      renderer UV fallback from authoritative atlas publication and state the
      generation/failure contract for dependent jobs.
- [ ] Document that the option remains default-off and that atlas failure
      leaves dependent bakes blocked rather than fabricating UV readiness.
- [ ] Update task indexes, session brief, and retirement records when the
      implementation and repeat verification are complete; do not rewrite
      archived task history.

## Acceptance criteria

The repair invariants below apply while the progressive path remains. For a
supersession closure, map each to replacement-path evidence or an explicit
removed-surface disposition; never require recreation of the deleted path.

- [ ] A currently needed affected path justifies the interim repair, or a
      landed replacement passes equivalent current-path regressions and
      supports an explicit supersession closure. A planned method is not proof.
- [ ] Successful progressive UV enrichment publishes finite, count-matched UVs
      with at least one nonzero-area mapped triangle and real atlas
      diagnostics; it never calls or retains a zero-fill authoring helper.
- [ ] Canonical corner-over-vertex resolution reads the one published UV
      authority; seams do not split authoritative source topology.
- [ ] Any newer UV, topology, position, entity, source-property, or binding
      generation causes a no-write stale discard that preserves user state
      exactly.
- [ ] Atlas failure is observable, does not fabricate authoritative
      UVs, and prevents dependent bake work from being reported ready
      or published.
- [ ] Raw geometry remains available immediately, while worker and main-thread
      publication ownership stay deterministic.
- [ ] The default runtime setting remains unchanged.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure \
  -R '^AssetWorkflowModule\.ProgressiveUvAtlas(PublishesCornerUvsWithoutTopologyChange|DiscardsCompletionAfterUserUvEdit|DiscardsCompletionAfterTopologyEdit|FailureLeavesBakeDependencyBlocked)$' \
  --no-tests=error --repeat until-fail:20 --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -R '^AssetWorkflowModule\.' --no-tests=error --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Calling a zero-filled property a generated UV atlas or treating property
  existence alone as atlas success.
- Applying completion after checking only entity validity or hard-coded
  generation values.
- Overwriting user-authored UVs, newer topology, positions, or source
  properties with stale worker output.
- Starting dependent bake publication after failed/stale atlas generation or
  enabling the progressive option by default as part of this fix.

## Maturity
- Target: `CPUContracted`.
- Closure requires real-atlas, failure, dependency, thread-ownership, and every
  stale-generation contract through the opt-in progressive runtime path.
- This task has no backend promotion claim; after those CPU contracts pass, no
  `Operational` follow-up is owed. GPU bake execution remains separately owned.

## Proposal — 2026-10-24 (awaiting operator decision)

Branch `claude/bug-097-remove-progressive-model-path-v2`, based on the
integration head `0d7b5b4d3`. It replaces the first proposal branch
`claude/bug-097-remove-progressive-model-path` (`311ad6938`, based on
`599f02ff1`), which stays unchanged; see "v2 on the integration head" below.
It is a proposal and is not merged. It removes the unreachable progressive
model-scene path instead of repairing it. No checkbox above is ticked, and the
start-gate item stays open until the operator decides.

### Start gate
- Currently needed workflow: none. No code in `src`, `tests`, `tools`,
  `benchmarks` or `methods` sets `ProgressiveRawGeometryFirst` or
  `ProgressiveJobs`. The only construction of
  `AssetWorkflowModelMaterializationOptions` is
  `Runtime.AssetWorkflowModule.cpp:598`. It uses designated initializers
  (`World`, `BindingEpoch`, `BindingValid`, `TextureBake`).
  `git log -S` shows that `0fdeac008` (2026-07-31) removed the last four
  setters together with `Test.AssetModelSceneHandoff.cpp`. Since then every
  `hasProgressiveJobs` / `ProgressiveRawGeometryFirst` branch has been
  constant-false in every composition.
- Replacement owner/branch: not identified. No task or branch matches the
  shared-atlas replacement. `METHOD-040`/`GEOM-076` were not assumed to be it.
- Options for the operator:
  - A: delete now (this branch).
  - B: keep the dead path until the replacement lands, then supersede.
  - C: interim repair. Not recommended, because the path is unreachable and
    there is nothing to reproduce.

  The first acceptance criterion names only interim repair or a landed
  replacement. Closing by removal without a replacement is therefore the
  operator's call.

### Change (deletion only; no behavior change in any composition)
- `Runtime.AssetWorkflowModelMaterialization.cppm`:
  - Removed the options `ProgressiveRawGeometryFirst` and `ProgressiveJobs`.
  - Removed the never-read counters `ProgressiveRawPrimitiveEntitiesPublished`,
    `ProgressiveUvAtlasJobsQueued` and `ProgressiveNormalJobsQueued`.
  - Removed the `JobService` import.
- `Runtime.AssetWorkflowModelMaterialization.cpp`:
  - Removed `WriteDefaultTexcoords`, `WriteDefaultVectorProperty`,
    `ProgressiveEnrichmentResult`, `QueueProgressiveNoopJob`,
    `MeshHasVertexNormals` and (v2 only) `MeshHasVertexTexcoords`.
  - Removed the uv and normal no-op jobs, the job-scheduled normal and albedo
    bake requests, and the geometry-only branch of `PreparePrimitives`.
  - Removed the `JobService` and `KernelEvents` imports and `<functional>`.
  - Each kept condition equals the old one without its constant-false arm:
    the early return, the normal arm, the albedo arm and the publication guard.
- `Runtime.AssetWorkflowGeometryMaterialization.cppm/.cpp`:
  - Removed `MeshPayloadHasValidVertexTexcoords` and
    `HasValidTexcoords(const MeshIOResult&)`. Their only caller was the
    deleted branch.
  - `BuildRuntimeHalfedgeMeshGeometryOnly` stays, because
    `Runtime.AssetWorkflowImportExecutor.cpp:1024` still calls it.
- `tools/repo/source_rules_engine_layering.py`: the `model-handoff` counts for
  `JobDesc ` and `.Scope = ` change from 3 to 0. The rule still fails if any
  job submission returns to this file: its self-check injects one token even
  when the expected count is 0.
- `docs/architecture/runtime.md`: dropped "or progressive" from the bake-extent
  fallback sentence.
- Size, not counting this note: 6 files, 10 insertions, 345 deletions (v1);
  10 insertions, 350 deletions (v2).

### Regression-first statement
Unreachable code cannot have a test that fails before its deletion, so no
test was written. The evidence instead:
- (a) No setter exists (see the start gate above).
- (b) The same suites give the same results before and after the change.
- (c) The source-rule count follows the deletion. The prediction was written
  before the run. After the deletion and before the rule edit,
  `check_source_rules.py --suite RuntimeEngineLayering` exited 1 with exactly
  two problems: "count of 'JobDesc ' is 0, expected 3" and
  "count of '.Scope = ' is 0, expected 3". After the edit it reported
  0 problems over 165 rules, and `--self-check` reported 0 problems over
  265 rules.

### Verification of v1 (at `599f02ff1`, `ci` preset)
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests`: exit 0,
  no warnings.
- Focused `ctest -R '^(AssetWorkflowModule|RuntimeAssetImportFormatCoverage|RuntimeAssetImportRecipe|RuntimeTextureBakeModule|AssetFormatCapabilities|RuntimeReuseDriftGuard)\.|^AssetCompilationLocality\.ModelMaterialization$|^RuntimeEngineLayering\.SourceRules$'`:
  109/109 passed after every executable was rebuilt.
  - The baseline at `599f02ff1` had 108/108.
  - The extra test is the slow-labeled
    `RuntimeAssetImportFormatCoverage.DirectMeshEnrichmentCloseDrainsGeneratedGridAndCompletesDeterministically`.
    Its executable did not exist yet when the baseline ran.
- `cmake --build --preset ci --target IntrinsicTests`: exit 0, no warnings.
- `ctest -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`: 5934/5934
  passed and 1 skipped (`GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl`),
  the same as the baseline.
  - The whole CPU suite ran because a `.cppm` interface changed.
  - GPU tests were excluded because no GPU code changed.
- The static checks all passed:
  - `check_layering --strict`: 0 violations.
  - `check_test_layout --strict`: 0 findings.
  - `check_doc_links`: no broken links.
  - `check_task_policy --strict`: 0 findings.
  - `generate_session_brief --check`: up to date.
  - `check_docs_sync --diff-mode --base-ref 599f02ff1`: satisfied.
- `rg` over `src tests tools` for every removed name: no hits.
- jscpd over the two touched `.cpp` files: 2 clones before, 1 after. The one
  left is pre-existing and unrelated.

### Acceptance criteria → removed-surface disposition (reviewers decide)
1. Needed path: none exists (see the start gate). Closing by removal is the
   operator's decision.
2. Zero-fill: the progressive UV enrichment is gone, and so are
   `WriteDefaultTexcoords` and `WriteDefaultVectorProperty`. `rg` finds no
   remaining zero-fill helper.
3. Canonical UVs: met only for the removed publisher. Model scenes get UVs
   only from the synchronous `BuildRuntimeHalfedgeMeshMaterialization`, which
   uses corner-over-vertex resolution and publishes seam UVs as `h:texcoord`.
   The kept bake gates read the canonical `HasResolvedTexcoords` since
   BUG-250; v2 keeps that.
4. Stale completion: model materialization has no deferred write left, so this
   file has no stale-completion window. Bake requests go to
   `TextureBakeService`, whose stale and unknown-source handling the
   `RuntimeTextureBakeModule` suite covers.
5. Atlas failure: the default path uses `RuntimeMeshUvFailurePolicy::Optional`.
   It counts `UvAtlasFailures` and writes no UVs.
   `RuntimeAssetImportFormatCoverage.RejectedAtlasDoesNotDiscardModelScene`
   asserts that no `v:texcoord` exists. Automatic bakes need resolved UVs
   (BUG-250's gate), so a failure blocks them instead of faking readiness;
   `ModelSceneSeamAtlasRequestsGeneratedTextureBakes` covers a rejected-atlas
   primitive.
6. Raw-first and thread ownership: raw-first mode is removed. Publication is
   one synchronous main-thread materialization, covered by
   `ExpectModelSceneCompletionRoute` (synchronous and queued) in
   `ModelSceneCompletionSelectsAndFramesCreatedPrimitives` and
   `AuthoredModelMaterialsUseOnlyPerRenderableLeases`.
7. Default setting: unchanged. The removed options were false or null in every
   composition.

The Tests checklist applies to the interim-repair branch only. Of the Docs
items, the `runtime.md` wording is fixed; indexes and retirement are left to
the integrator.

### Finding for the 09:00 decision (resolved by BUG-250)
The finding below was filed as option (i), BUG-250, and fixed in
`9c5b4559e` (`tasks/done/BUG-250-direct-texture-bakes-skip-seam-atlas-models.md`).
The v1 text is kept as the record; its line numbers refer to v1.
- What the code does:
  - The kept direct-bake branches gate on `MeshHasVertexTexcoords`
    (`Runtime.AssetWorkflowModelMaterialization.cpp:882`, used at `:1032` for
    the normal bake and `:1073` for albedo). It reads only `v:texcoord`.
  - A seam-bearing generated atlas publishes only `h:texcoord` and removes
    `v:texcoord` (`Runtime.AssetWorkflowGeometryMaterialization.cpp:582`,
    `:927`).
  - `TextureBakeService` accepts `h:texcoord`
    (`Runtime.TextureBakeModule.cpp:415`, `:572`). The direct-mesh path checks
    both properties.
  - `docs/architecture/property-coherence.md:437-440` says that watching only
    `v:texcoord` is wrong for seam meshes.
- Likely effect (inferred from the code, not run): model primitives with a
  seam atlas never get the automatic normal or albedo bake request. Their
  normal slot stays at "waiting for resolved UVs and vertex normals ...". No
  CPU test targets the model-scene direct normal-bake branch.
- Options:
  - (i) File a new bug.
  - (ii) Extend BUG-097's scope.

  Candidate fix for either: gate on the canonical
  `PreparedPrimitive::HasResolvedTexcoords` instead, and add a seam-atlas
  model-scene test that asserts a bake request.

### Defaults chosen
- The helper cleanup in `GeometryMaterialization` is included.
- `ProgressiveTextureBakeJobsQueued`, `RecordProgressiveTextureBakeDiagnostic`
  and the name `QueueProgressiveEnrichmentJobs` are kept. They are on the live
  path, and renaming them is out of scope.
- The source rule is kept with count 0 rather than deleted.
- ADR 0021 and rule 5 of the import-visibility skill ("publish decoded raw
  geometry first") are unchanged. They record history and principle, and the
  model-scene raw-first seam was never wired.
- If model scenes need an async atlas, the base to reuse is the direct-mesh
  deferred atlas job in `Runtime.AssetWorkflowRecipePolicies.cpp`.

### v2 on the integration head (2026-10-24)
- Why: `311ad6938` rewrites the two direct-bake gates that BUG-250
  (`9c5b4559e`) changed. Merging it as it was would revert BUG-250.
- How: `git switch -c claude/bug-097-remove-progressive-model-path-v2
  0d7b5b4d3`, then `git cherry-pick 311ad6938` (the only commit on the old
  branch). It conflicted only in `Runtime.AssetWorkflowModelMaterialization.cpp`.
- Resolution, in the cherry-pick commit itself (a split would leave a commit
  that either does not build or reverts BUG-250):
  - Normal and albedo gates: the progressive arm is gone as the proposal
    intends; the condition is BUG-250's `primitive.HasResolvedTexcoords`,
    unchanged.
  - `MeshHasVertexTexcoords` is removed as well. After BUG-250 its only
    caller was the deleted progressive UV-job guard
    (`if (hasProgressiveJobs && !MeshHasVertexTexcoords(primitive.Mesh))`).
  - The one extra focused test against v1 (110 vs 109) is
    `RuntimeAssetImportFormatCoverage.ModelSceneSeamAtlasRequestsGeneratedTextureBakes`
    (checked by name by the independent review of `3fb7ae908`).
- Verification (this session, `build/ci`):
  - `IntrinsicRuntimeContractTests` build: exit 0. No warnings in
    `AssetWorkflow` files; the warnings in the log are in unrelated files.
  - The v1 focused `ctest -R` set with `-LE 'gpu|vulkan|flaky-quarantine'`:
    110/110 passed (v1: 109; the extra one is BUG-250's
    `RuntimeAssetImportFormatCoverage.ModelSceneSeamAtlasRequestsGeneratedTextureBakes`).
  - Mutation of the resolution: both gates set back to a `v:texcoord` check
    made that test fail (normal slot "waiting for resolved UVs and vertex
    normals ...", albedo slot "waiting for vertex color albedo bake"). With
    the resolution restored it passed.
  - `IntrinsicTests` build: exit 0, no warnings in `AssetWorkflow` files.
    `ctest -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j12`:
    5942 tests, 0 failed, 1 skipped
    (`GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl`, as in v1).
  - `check_layering --strict`: 0 violations. `check_test_layout --strict`:
    0 findings. `check_doc_links --strict`: no broken links.
    `check_task_policy --strict`: 0 findings.
    `check_source_rules.py --suite RuntimeEngineLayering`: 0 problems over
    165 rules; `--self-check`: 0 problems over 265 rules;
    `Test.CheckSourceRules.py`: ok. `generate_session_brief --check`: up to
    date. `check_docs_sync --diff-mode --base-ref 0d7b5b4d3`: satisfied.
  - `rg` over `src tests tools` for every removed name, including
    `MeshHasVertexTexcoords`: no hits.

### Remaining
- The operator's choice of A, B or C.
- An independent correctness review of the diff.
- Index and retirement records, which belong to the integrator.
