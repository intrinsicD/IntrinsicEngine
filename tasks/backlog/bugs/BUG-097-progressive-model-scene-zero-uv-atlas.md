---
id: BUG-097
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-097 — Progressive model-scene UV job publishes a zero atlas

## Goal
- Make the default-off progressive model-scene UV job publish a real,
  generation-valid atlas or fail closed, while guaranteeing that a stale
  completion cannot overwrite user-authored UVs or newer geometry.

## Non-goals
- No enabling `ProgressiveRawGeometryFirst` by default.
- No zero-filled authoritative `v:texcoord` placeholder.
- No synchronous UV generation on the initial raw-publication path.
- No new UV-unwrapping method when the existing runtime halfedge
  materialization/atlas seam can satisfy the contract.
- No GPU texture-bake implementation or unrelated progressive-job redesign.

## Context
- Owner: runtime progressive model-scene materialization and derived-job
  generation validation. Geometry owns the CPU atlas method; runtime owns ECS
  snapshots, scheduling, dependency state, publication, and stale discard.
- `WriteDefaultTexcoords()` currently fills every `v:texcoord` with `(0, 0)`.
  The progressive job is named `generate mesh uv atlas` but calls that helper,
  so a completed job claims an atlas while publishing a degenerate
  authoritative property.
- The queued helper hard-codes entity, geometry, source-property, and binding
  generations and has no main-thread validation callback. Its apply lambda
  checks only entity validity. A user can parameterize UVs, edit topology or
  positions, or replace bindings before completion and then lose that newer
  work to the zero-filled result.
- Existing
  `RuntimeAssetModelSceneHandoff.ProgressiveRawGeometryFirstPublishesNormalsAndQueuesUvAndBakeJobs`
  asserts only that `v:texcoord` eventually exists. It does not require finite
  non-degenerate UV triangles, atlas provenance, stale discard, or preservation
  of authored UVs.
- The progressive option is currently default-off in `Runtime.Engine`, so this
  is a latent opt-in correctness defect rather than evidence that the viewer's
  default model path runs progressive enrichment.
- Archived progressive-enrichment work requires real async atlas output and
  generation-safe derived publication. Archived UV-fallback guidance allows
  renderer-local zero UVs for raw packing only; it explicitly does not justify
  writing zero UVs into authoritative ECS geometry or starting bake work that
  requires a real atlas.

## Required changes
- [ ] Replace the zero-fill worker body with the existing CPU halfedge
      materialization/UV-atlas path over an immutable snapshot of positions,
      topology, and relevant source properties.
- [ ] Return an explicit success/failure payload containing finite,
      count-matched atlas coordinates and diagnostics/provenance sufficient to
      distinguish real atlas output from a renderer fallback.
- [ ] Capture authoritative entity, geometry, source-property, and binding
      generations at submission and validate all of them on the main thread
      immediately before publication.
- [ ] On any generation mismatch, report stale discard and perform no geometry
      or property write. In particular, preserve exact UVs authored after
      submission and never merge the older atlas into changed topology.
- [ ] On atlas failure, leave authoritative `v:texcoord` absent (or preserve a
      pre-existing valid property), report the failure, and keep dependent
      texture-bake jobs blocked/failed rather than marking the UV dependency
      ready.
- [ ] Preserve non-UV source properties and initial raw publication. Worker
      computation stays off the main thread; validated publication stays on
      the main thread.

## Tests
- [ ] Regression first: strengthen
      `RuntimeAssetModelSceneHandoff.ProgressiveRawGeometryFirstPublishesNormalsAndQueuesUvAndBakeJobs`
      in `tests/contract/runtime/Test.AssetModelSceneHandoff.cpp`. Assert UV
      count, finiteness, real atlas diagnostics, and nonzero signed or absolute
      UV area for at least one non-degenerate source triangle.
- [ ] Add
      `RuntimeAssetModelSceneHandoff.ProgressiveUvAtlasDiscardsCompletionAfterUserUvEdit`.
      Hold completion deterministically, write distinctive user UVs, release
      it, and assert exact preservation plus a stale-discard status.
- [ ] Add
      `RuntimeAssetModelSceneHandoff.ProgressiveUvAtlasDiscardsCompletionAfterTopologyEdit`
      and assert no old atlas is applied to the newer vertex/topology
      generation.
- [ ] Add
      `RuntimeAssetModelSceneHandoff.ProgressiveUvAtlasFailureLeavesBakeDependencyBlocked`
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
- [ ] Successful progressive UV enrichment publishes finite, count-matched UVs
      with at least one nonzero-area mapped triangle and real atlas
      diagnostics; it never calls or retains a zero-fill authoring helper.
- [ ] Any newer UV, topology, position, entity, source-property, or binding
      generation causes a no-write stale discard that preserves user state
      exactly.
- [ ] Atlas failure is observable, does not create authoritative
      `v:texcoord`, and prevents dependent bake work from being reported ready
      or published.
- [ ] Raw geometry remains available immediately, while worker and main-thread
      publication ownership stay deterministic.
- [ ] The default runtime setting remains unchanged.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeAssetModelSceneHandoff\.(ProgressiveRawGeometryFirstPublishesNormalsAndQueuesUvAndBakeJobs|ProgressiveUvAtlasDiscardsCompletionAfterUserUvEdit|ProgressiveUvAtlasDiscardsCompletionAfterTopologyEdit|ProgressiveUvAtlasFailureLeavesBakeDependencyBlocked)$' \
  --repeat until-fail:20 --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeAssetModelSceneHandoff\.Progressive' --timeout 60
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
