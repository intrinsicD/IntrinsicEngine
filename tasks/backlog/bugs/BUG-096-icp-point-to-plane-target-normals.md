---
id: BUG-096
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-096 — ICP point-to-plane ignores target normals

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
- Both synchronous and queued runtime registration call
  `AlignPointClouds(prealignedSourceWorld, targetWorld, {}, params)`, passing an
  empty target-normal span even when the target has `v:normal`.
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
- [ ] Read the target point cloud's authoritative `v:normal` property into the
      same immutable registration snapshot as target positions for both
      synchronous and queued execution.
- [ ] Validate property presence, element type/count, finiteness, nonzero
      length, and target transform invertibility before dispatch. A
      point-to-plane request with invalid normals must fail closed with a
      stable prerequisite/status reason and must not mutate the source.
- [ ] Transform target normals into world space with the inverse-transpose
      normal transform and normalize them before calling
      `Geometry::AlignPointClouds`.
- [ ] Pass the validated target-normal span through synchronous and derived-job
      paths. Include its source-property generation in queued staleness
      validation so a normal edit before apply discards the result.
- [ ] Record both requested and effective registration variants, or otherwise
      return an equivalent truthful result contract. Runtime must never report
      point-to-plane success when geometry executed point-to-point.
- [ ] Preserve existing point-to-point behavior without requiring normals.

## Tests
- [ ] Regression first: add
      `SandboxEditorUi.RegistrationPointToPlaneUsesTargetNormals` in
      `tests/contract/runtime/Test.SandboxEditorClusteringMethods.cpp` with a
      deterministic point set/normal field whose point-to-plane update differs
      measurably from point-to-point. Assert requested/effective variant,
      convergence diagnostics, source transform, and undo/redo.
- [ ] Add
      `SandboxEditorUi.QueuedRegistrationPointToPlaneUsesTargetNormals` through
      the real derived-job path and assert parity with the synchronous result
      within the declared numerical tolerance.
- [ ] Add fail-closed cases for absent, wrong-count, wrong-type, non-finite, and
      zero-length target normals. Assert no source mutation, no command-history
      entry, and a stable actionable status for each case.
- [ ] Add a rotated and non-uniformly scaled target case that proves normals use
      inverse-transpose transformation rather than position/vector
      transformation.
- [ ] Add a queued stale case that edits target normals after submission and
      proves the completion is discarded without applying the old transform.
- [ ] Extend `tests/unit/runtime/Test.RegistrationAlignment.cpp` to pin
      requested/effective variant reporting and preserve point-to-point
      behavior with an empty normal span.

## Docs
- [ ] Document the runtime point-to-plane prerequisites, world-space normal
      conversion, fail-closed behavior, and requested/effective result fields
      in the registration/editor runtime documentation.
- [ ] Update the geometry registration contract documentation only if its
      public fallback/result semantics change.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if a module surface
      changes, then update task indexes, session brief, and retirement records
      when verified.

## Acceptance criteria
- [ ] Both synchronous and queued point-to-plane commands pass finite,
      count-matched, normalized world-space target normals to geometry.
- [ ] A distinguishing regression proves the effective solver is
      point-to-plane rather than a numerically coincident point-to-point run.
- [ ] Missing or invalid target normals fail before dispatch with an actionable
      reason, no source mutation, and no undo-history entry.
- [ ] Target-normal edits invalidate queued results, and non-uniform target
      transforms produce the analytically expected normal directions.
- [ ] Requested/effective variants are truthful for every terminal result;
      silent fallback cannot be displayed as point-to-plane success.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci \
  --target IntrinsicRuntimeContractTests IntrinsicRuntimeUnitTests
ctest --test-dir build/ci --output-on-failure \
  -R '^SandboxEditorUi\.(RegistrationPointToPlaneUsesTargetNormals|QueuedRegistrationPointToPlaneUsesTargetNormals|RegistrationPointToPlaneRejectsInvalidTargetNormals|QueuedRegistrationPointToPlaneDiscardsStaleTargetNormals)$|^RuntimeRegistrationAlignment\.' \
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
