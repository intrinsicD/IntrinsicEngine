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
