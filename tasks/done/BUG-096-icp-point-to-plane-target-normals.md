---
id: BUG-096
theme: G
depends_on: [RUNTIME-192, RUNTIME-194]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude-bug096"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-09T11:40:00Z"
maturity_target: CPUContracted
contract_schema: 1
contracts:
  - geometry.element-domain-sources
contract_review: >-
  The operation consumes a typed v:normal property on point-cloud vertices from
  an ECS geometry entity and gates eligibility on that element-domain
  availability, which is exactly geometry.element-domain-sources. It publishes
  nothing back to a geometry source — the only write is the source entity's
  Transform — so geometry.property-coherence does not apply.
---
# BUG-096 — ICP point-to-plane ignores target normals

## Status

- Completed and retired on 2026-08-09.
- Completion commit: this retirement commit.

## Progress — landed 2026-08-09

Both branches now resolve the target's `v:normal` before anything is dispatched
or mutated, and a point-to-plane request that cannot be satisfied is **refused**
rather than degraded. That choice is the whole fix: `AlignICP` degrades silently
by contract, so the only way to stop the editor displaying point-to-plane
success for a point-to-point run is to never hand the solver a request it will
degrade. `EffectiveVariant` and `TargetNormalCount` make the guarantee legible
instead of merely true.

Normals are carried to world space by the inverse transpose of the target
model's linear part and normalized; the queued branch snapshots them in local
space alongside the positions and converts them in the worker, so a normal edit
between submit and apply makes the job stale by the same comparison the
positions already had.

### Why the distinguishing regression took four attempts

The task asked for a regression that proves the effective solver is
point-to-plane rather than a numerically coincident point-to-point run. Three
plausible constructions do not distinguish anything, and each one is worth
recording because the next person will reach for them:

- **A pure translation.** The runtime pre-aligns centroids before ICP, so the
  offset is gone before the solver runs.
- **A uniform scale about the centroid.** Its optimal rigid fit is the identity
  under both metrics.
- **A planar or near-planar target.** Its normals span a narrow cone, so the
  6-DOF point-to-plane system is rank-deficient, `SolvePointToPlane`'s Cholesky
  bails to the identity increment, and two *different* normal fields produce
  bit-identical results however faithfully they were passed through.

A sphere fixes the last one — its normals span every direction, which is why the
geometry-level point-to-plane tests use one. And because the data is exactly
registrable, all metrics converge to the same rotation, so the endpoint cannot
separate them either: the evidence is the **convergence path**. Two
point-to-plane runs differing only in the target normal field take different
iteration counts to different residuals, and a point-to-plane run differs from
point-to-point the same way. The regression was confirmed to fail against a
tree with only the solver hand-off reverted.

## Goal
- Make runtime point-to-plane ICP consume finite, count-matched target normals
  in world space or fail with an explicit prerequisite/result status; never
  silently run point-to-point while reporting point-to-plane.

## Non-goals
- No normal estimation algorithm added implicitly to the registration command.
- No requirement for source normals; point-to-plane uses target normals.
- No silent fallback chosen merely to keep the command enabled.
- No broad editor prerequisite-tooltip implementation; the dependent UI tasks
  consume the truthful runtime readiness/result contract.
- No GPU registration backend or performance claim.

## Context
- Owner: runtime registration snapshot, validation, and result reporting over
  the public geometry registration contract. Geometry owns ICP mathematics;
  runtime owns ECS property lookup, world-space conversion, async snapshot
  validity, and editor-facing command/result state.
- Both synchronous and queued branches in `ApplyEditorRegistrationCommand`
  call `Geometry::Registration::AlignICP` with an empty target-normal span even
  when the target has `v:normal`.
- `Geometry.Registration` currently changes the effective variant from
  `PointToPlane` to `PointToPoint` when target normals are empty or
  count-mismatched. The runtime result retains the requested command variant,
  so the editor can report point-to-plane although point-to-point actually ran.
- A valid point-to-plane runtime input requires two distinct live point-cloud
  entities, at least three finite source and target positions, and a finite
  target `v:normal` property with exactly one vector per target point. Normals
  must be transformed by the target world transform's normal transform
  (inverse transpose), normalized, and rejected when non-finite or
  zero-length, including under non-uniform scale.
- Existing runtime registration tests exercise point-to-point only. They cover
  sync, queued, stale, invalid, undo/redo, and entity transforms, but therefore
  cannot distinguish the requested and effective point-to-plane variants.

## Required changes
- [x] Resolve the target point cloud's authoritative `v:normal` through the
      canonical `GeometryPropertyRef`/catalog and copy it into the same
      immutable registration snapshot as target positions for every execution.
      Read through `GS::PropertyNames::kNormal` on the target's vertex source;
      the queued branch stores it on `EditorRegistrationCpuJobState` next to
      the local positions.
- [x] Validate property presence, element type/count, finiteness, nonzero
      length, and target transform invertibility before dispatch. A
      point-to-plane request with invalid normals must fail closed with a
      stable prerequisite/status reason and must not mutate the source.
      `RegistrationNormalStatus` names each cause and
      `BuildRegistrationNormalRejectionMessage` turns it into an actionable
      sentence.
- [x] Transform target normals into world space with the inverse-transpose
      normal transform and normalize them before calling
      `Geometry::Registration::AlignICP`. A normal that becomes non-finite or
      zero-length under that transform is rejected the same way.
- [x] Pass the validated target-normal span through the typed registration
      operation on `JobService`. Include its source-property generation in
      staleness validation so a normal edit before apply discards the result;
      do not retain a second synchronous/derived-job command implementation.
      `ValidateRegistrationCpuJobApply` re-reads the target normals and
      compares them against the snapshot.
- [x] Record both requested and effective registration variants, or otherwise
      return an equivalent truthful result contract. Runtime must never report
      point-to-plane success when geometry executed point-to-point.
      `EditorRegistrationResult::EffectiveVariant` plus `TargetNormalCount`;
      the panel renders `Variant: <requested> (ran <effective>)`.
- [x] Preserve existing point-to-point behavior without requiring normals.
      A point-to-point request never reads the normal property and reports
      `TargetNormalCount == 0`.

## Tests
- [x] Regression first: add
      `SandboxEditorUi.RegistrationPointToPlaneUsesTargetNormals` in
      `tests/contract/runtime/Test.SandboxEditorClusteringMethods.cpp` with a
      deterministic point set/normal field whose point-to-plane update differs
      measurably from point-to-point. The distinguishing signal is the
      convergence path rather than the source transform — see `## Progress` for
      why the transform cannot separate them on exactly-registrable data.
- [x] Add
      `SandboxEditorUi.QueuedRegistrationPointToPlaneUsesTargetNormals` through
      the real derived-job path and assert parity with the synchronous result
      within the declared numerical tolerance. It also carries its own
      point-to-point control, because parity alone would still pass if both
      branches dropped the normals.
- [x] Add fail-closed cases for absent, wrong-count, wrong-type, non-finite, and
      zero-length target normals. Assert no source mutation, no command-history
      entry, and a stable actionable status for each case
      (`RegistrationPointToPlaneRejectsInvalidTargetNormals`).
- [x] Add a rotated and non-uniformly scaled target case that proves normals use
      inverse-transpose transformation rather than position/vector
      transformation
      (`RegistrationPointToPlaneTransformsNormalsByInverseTranspose`).
- [x] Add a queued stale case that edits target normals after submission and
      proves the completion is discarded without applying the old transform
      (`QueuedRegistrationPointToPlaneDiscardsStaleTargetNormals`), matching the
      established `JobState::StaleDiscarded`/no-sink convention.
- [x] Pin requested/effective variant reporting and point-to-point behavior in
      the typed registration-operation tests. `RUNTIME-202` deleted the thin
      standalone wrapper; extend `GeometryProcessingOperations` directly.

## Docs
- [x] Document the runtime point-to-plane prerequisites, world-space normal
      conversion, fail-closed behavior, and requested/effective result fields
      in the registration/editor runtime documentation
      (`src/runtime/README.md`).
- [x] Update the geometry registration contract documentation only if its
      public fallback/result semantics change. It did not: geometry keeps its
      documented fallback, and runtime refuses to hand it a request that would
      trigger it.
- [x] Regenerate `docs/api/generated/module_inventory.md` if a module surface
      changes, then update task indexes, session brief, and retirement records
      when verified. The inventory is unchanged (no module names moved).

## Acceptance criteria
- [x] Both synchronous and queued point-to-plane commands pass finite,
      count-matched, normalized world-space target normals to geometry.
- [x] A distinguishing regression proves the effective solver is
      point-to-plane rather than a numerically coincident point-to-point run.
      Confirmed to fail against a tree with only the solver hand-off reverted.
- [x] Missing or invalid target normals fail before dispatch with an actionable
      reason, no source mutation, and no undo-history entry.
- [x] Target-normal edits invalidate queued results, and non-uniform target
      transforms produce the analytically expected normal directions.
- [x] Requested/effective variants are truthful for every terminal result;
      silent fallback cannot be displayed as point-to-plane success.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure \
  -R '^SandboxEditorUi\.(RegistrationPointToPlaneUsesTargetNormals|QueuedRegistrationPointToPlaneUsesTargetNormals|RegistrationPointToPlaneRejectsInvalidTargetNormals|QueuedRegistrationPointToPlaneDiscardsStaleTargetNormals)$' \
  --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src \
  --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Passing an empty normal span for a point-to-plane request.
- Falling back to point-to-point without exposing that effective variant in the
  result and editor-visible state.
- Transforming normals with the target position matrix or accepting
  non-finite, zero-length, or count-mismatched data.
- Estimating normals silently or requiring source normals to make the command
  appear ready.

## Maturity
- Target: `CPUContracted`.
- Closure requires sync/queued parity, transform correctness, malformed-normal
  rejection, truthful result reporting, and stale-normal generation coverage.
- Registration has no backend promotion axis in this task; after these CPU
  contracts pass, no `Operational` follow-up is owed.
