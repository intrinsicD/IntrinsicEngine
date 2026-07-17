---
id: BUG-095
theme: G
depends_on: []
maturity_target: Operational
---
# BUG-095 — Direct-mesh postprocess can overwrite newer editor geometry

## Goal
- Make asynchronous direct-mesh import enrichment generation-safe so a stale
  completion can never overwrite newer editor positions, topology, UVs, or
  properties, and expose pending enrichment through the standard action
  readiness model.

## Non-goals
- No synchronous conversion of the import postprocess merely to avoid races.
- No preservation rule limited to vertex normals; every authoritative geometry
  and source-property edit participates in one generation contract.
- No UI-private job state or duplicate readiness validation in `app`.
- No sleeps, timing luck, retry-until-green behavior, or tests that depend on a
  large user-provided model.
- No redesign of unrelated derived-job scheduling or editor command history.

## Context
- Owner: runtime import policy, streaming completion, geometry generation, and
  selected-action readiness. `app` may render the runtime-owned readiness DTO
  but must not own or recompute the stale-completion decision.
- The default direct-mesh policy captures the original import payload, runs a
  `StreamingExecutor` postprocess, and later replaces the live mesh through
  `PopulateFromMesh()`. Its apply path checks result and entity liveness but
  does not compare the geometry, source-property, or binding generation that
  was current at submission.
- The apply path has a one-off attempt to snapshot and restore vertex normals
  when counts remain compatible. Newer position edits, topology-changing
  remesh/subdivide/simplify operations, UV parameterization, and arbitrary
  source properties can still be lost; a topology edit can also make the
  normal-only preservation path inapplicable.
- Import completion selects and focuses the raw entity before enrichment
  finishes. The editor processing model currently advertises conflicting
  mutating commands as ready even though a pending completion can replace their
  output afterward.
- Existing coverage
  `SandboxEditorUi.MeshVertexNormalsCommandSurvivesPendingDirectMeshPostProcess`
  locks only the normal special case. It does not coordinate submission/apply
  deterministically, mutate positions or topology, or assert an observable
  stale-discard result.
- The 2026-07-16 local `child.obj` audit also proved that the same pending
  completion remains live while every mesh mutation panel is advertised. Its
  avoidable multi-minute CPU cost and close-time drain were fixed by
  `BUG-101`; this task still owns stale-write prevention and pending action
  readiness now that the work completes in bounded time.

## Required changes
- [ ] Introduce the smallest runtime-owned direct-mesh enrichment generation
      key using the existing entity, geometry, source-property, and binding
      generations/signatures already authoritative for derived jobs.
- [ ] Capture the key with the immutable worker snapshot at submission and
      validate it on the main thread immediately before apply. A mismatch must
      produce an observable `StaleDiscarded`/cancelled status and leave every
      current ECS geometry and source property byte-for-byte unchanged.
- [ ] Remove the vertex-normal-only overwrite workaround once the general
      generation guard covers the same case; do not merge stale output
      selectively into newer topology.
- [ ] Ensure entity destruction/replacement and source binding changes use the
      same fail-closed completion path and cannot target a recycled entity.
- [ ] Surface pending direct-mesh enrichment, and its non-empty reason, in the
      runtime selected-entity processing/action-readiness model. Conflicting
      geometry-mutating actions must remain disabled until apply, failure, or
      stale discard resolves the pending state.
- [ ] Keep initial raw publication, selection, focus, worker execution, and
      main-thread apply asynchronous. Do not block the frame loop waiting for
      enrichment.

## Tests
- [ ] Regression first: replace or generalize the existing normal-only
      regression in
      `tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp` with
      `SandboxEditorUi.DirectMeshPostProcessDiscardsCompletionAfterGeometryEdit`.
      Use a deterministic worker/apply barrier, edit positions and topology
      after submission, release completion, and assert the edited mesh,
      generations, undo history, and selection remain unchanged.
- [ ] Add
      `SandboxEditorUi.DirectMeshPostProcessPreservesNewerUvAndSourceProperties`
      using a small generated fixture. Author non-default UVs and an arbitrary
      vertex property before deferred apply, then assert exact values and an
      observable stale-discard diagnostic after pumping maintenance.
- [ ] Retain explicit coverage that a completion with an unchanged generation
      applies once, publishes enriched output, advances the expected
      generation, and resolves pending state.
- [ ] Add
      `SandboxEditorUi.DirectMeshPostProcessPendingStateGatesMutatingActions`
      to assert readiness is false with a non-empty pending reason between
      submission and resolution, then true after successful apply, failure, or
      stale discard.
- [ ] Exercise the real `Engine`, direct import policy, `StreamingExecutor`,
      completion queue, and Null backend; the tests must coordinate with a
      barrier/test seam rather than wall-clock sleeps.

## Docs
- [ ] Document the direct-mesh enrichment generation key, stale-discard
      semantics, and pending action-readiness ownership in the runtime import
      documentation.
- [ ] Remove documentation that implies postprocess completion may merge into
      arbitrarily newer authoritative mesh state.
- [ ] Update task indexes, session brief, and retirement records when the
      implementation and repeat verification are complete.

## Acceptance criteria
- [ ] A position, topology, UV, property, entity-generation, or source-binding
      change after submission makes the old completion stale and prevents every
      write from that completion.
- [ ] Stale discard preserves the exact newer geometry, properties, selection,
      command history, and generation values and emits an actionable terminal
      status rather than silently pretending to apply.
- [ ] An unchanged completion still applies asynchronously exactly once.
- [ ] Runtime action readiness reports pending enrichment consistently and
      prevents conflicting mutating commands until the terminal completion is
      observed.
- [ ] Deterministic real-engine contracts pass repeatedly without sleeps, data
      races, user datasets, or a production test-only delay.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure \
  -R '^SandboxEditorUi\.(DirectMeshPostProcessDiscardsCompletionAfterGeometryEdit|DirectMeshPostProcessPreservesNewerUvAndSourceProperties|DirectMeshPostProcessPendingStateGatesMutatingActions)$' \
  --repeat until-fail:20 --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -R '^SandboxEditorUi\.(DirectMeshPostProcessDiscardsCompletionAfterGeometryEdit|DirectMeshPostProcessPreservesNewerUvAndSourceProperties|DirectMeshPostProcessPendingStateGatesMutatingActions)$' \
  --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Restoring selected properties from a stale result while allowing the stale
  mesh body or topology to replace newer state.
- Checking only entity validity, vertex count, or vertex normals as a proxy for
  the authoritative generation key.
- Allowing `app` widgets to infer pending state from private job internals or
  bypass the runtime readiness model.
- Disabling all editor processing permanently or making direct import
  postprocessing synchronous.

## Maturity
- Target: `Operational`.
- `CPUContracted` requires deterministic stale-discard and readiness contracts
  for every mutation class plus unchanged-generation success.
- `Operational` requires those contracts through the real `Engine`,
  `StreamingExecutor`, main-thread completion queue, and Null runtime path. No
  Vulkan-specific proof is owed because the overwrite boundary is
  backend-neutral.
