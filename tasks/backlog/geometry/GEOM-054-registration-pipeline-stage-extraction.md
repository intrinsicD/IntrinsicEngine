---
id: GEOM-054
theme: none
depends_on: []
---
# GEOM-054 — Registration pipeline: extract named ICP stages (zero behavior change)

## Goal
- Reorganize the monolithic `Geometry::Registration::AlignICP` loop into an
  explicit, named internal stage sequence (correspondence → rejection → robust
  weighting → transform solve → convergence) with **bit-for-bit identical
  outputs**, preparing the swappable-stage seam described in
  [`docs/architecture/geometry-pipeline-modularity.md`](../../../docs/architecture/geometry-pipeline-modularity.md)
  without adding any new capability. This is Slice 0 of that roadmap.

## Non-goals
- No public `Geometry.Registration` `.cppm` surface change: `AlignICP`'s
  signature, `RegistrationParams`, and `RegistrationResult` stay exactly as they
  are today. (The public `ConvergenceCriteria` struct and backend telemetry are
  Slice 2.)
- No new correspondence/rejector/transform variants, no `RejectorChain`, no
  global/coarse stage, no non-rigid path, no multi-scale schedule (Slices 1–7).
- No `Backend`/`RequestedBackend`/`ActualBackend`/`FellBackToCPU` telemetry.
- No changes to `Runtime.SandboxEditorUi` or any runtime/UI code.
- No behavior change of any kind — this is a pure mechanical/structural refactor.

## Context
- Owner layer: `geometry` (`geometry -> core` only; no assets/ecs/graphics/rhi/
  runtime/platform/app imports).
- Current state: `AlignICP` (`Geometry.Registration.cppm:145`) runs a single loop
  (`Geometry.Registration.cpp:450`) that fuses four already-named anonymous-namespace
  helpers — `FindCorrespondences` (`:48`), `RejectOutliers` (`:114`),
  `ApplyRobustWeights`, `SolvePointToPoint`/`SolvePointToPlane` (dispatched on
  `ICPVariant` at `:476`) — plus an inline relative-RMSE convergence check (`:491`).
  The four helpers map 1:1 onto the reference-model stages (§4 of the design doc);
  this slice makes that structure explicit in code so later slices can swap each
  axis via the Algorithm-Variant-Dispatch idiom
  ([`algorithm-variant-dispatch.md`](../../../docs/architecture/algorithm-variant-dispatch.md)).
- Coordinates with the geometry umbrella
  [`RORG-031E`](RORG-031-geometry-method-readiness.md); listed in
  [`README.md`](README.md).

<!-- Control surfaces / backends unchanged by this slice. -->
## Backends
- Backend axis: deferred to Slice 2 of the design doc; not introduced here.

## Required changes
- [ ] In `src/geometry/Geometry.Registration.cpp`, reorganize the ICP loop body
      into a clearly named internal stage sequence (e.g. an internal
      `RunIcpIteration`/`RunIcpLoop` that calls named stage functions for
      correspondence, rejection, robust weighting, transform solve, and
      convergence evaluation). Preserve the existing anonymous-namespace helper
      bodies verbatim where possible; only restructure call flow and naming.
- [ ] Keep the convergence test as a named internal helper (e.g.
      `EvaluateConvergence`) reading the existing `RegistrationParams` fields; do
      **not** promote it to a public struct in this slice.
- [ ] Add short comments mapping each internal stage to the reference-model
      interface number (§2 of the design doc) so the swap points are documented.
- [ ] Confirm `AlignICP`'s public signature and the `RegistrationParams`/
      `RegistrationResult` layouts are untouched (no `.cppm` edits beyond
      comments, if any).

## Tests
- [ ] The existing `Registration_ICP.*` suite in
      `tests/unit/geometry/Test_Registration.cpp` (23 cases, incl.
      `RecoversRigidTransform_PointToPoint/PointToPlane`,
      `RMSEHistoryIsMonotonicallyDecreasing`, `ResultFieldsArePopulated`) must
      pass **unchanged** — this is the correctness proof for the refactor.
- [ ] Add a golden-stability regression case asserting `Transform`, `FinalRMSE`,
      and `IterationsPerformed` are unchanged on a fixed synthetic source/target
      pair (both ICP variants), only if the existing suite does not already pin
      these numerically.
- [ ] Run the default CPU correctness gate for the touched scope.

## Docs
- [ ] `docs/architecture/geometry-pipeline-modularity.md` is added in this change
      (roadmap doc; registered in `docs/architecture/index.md`).
- [ ] No `docs/api/generated/module_inventory.md` regeneration is required
      because the public module surface is unchanged; confirm the inventory diff
      is empty.
- [ ] Keep [`README.md`](README.md) and
      [`RORG-031E`](RORG-031-geometry-method-readiness.md) child inventory
      consistent (updated in this change); regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria
- [ ] `Registration_ICP.*` outputs are bit-for-bit identical to pre-slice
      behavior (existing suite green; golden-stability assertion green).
- [ ] No public `Geometry.Registration` surface change; module inventory diff is
      empty.
- [ ] `python3 tools/repo/check_layering.py --root src --strict` passes (no new
      dependency edges).
- [ ] Each internal ICP stage is named and its reference-model mapping documented
      in a comment.
- [ ] `python3 tools/agents/validate_tasks.py --root . --strict` and
      `python3 tools/agents/check_task_policy.py --root . --strict` pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Registration_ICP' --timeout 60

# Structural / policy gates
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .

# Confirm no public surface drift (inventory must be unchanged):
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
git diff --exit-code docs/api/generated/module_inventory.md
```

## Forbidden changes
- Mixing this mechanical/structural move with any semantic algorithm change.
- Changing the public `Geometry.Registration` `.cppm` surface (params/result/
  signature), or adding new variants, telemetry, global/coarse, or non-rigid paths.
- Touching `Runtime.SandboxEditorUi` or any runtime/UI/ECS code.
- Introducing any dependency that violates `geometry -> core`.

## Maturity
- Target: `Operational` — this is a behavior-preserving refactor of an
  already-operational CPU path; the existing `Registration_ICP.*` suite exercises
  it. No new capability ships here.
- The swappable-stage capability (`CorrespondenceKind`, `RejectorChain`,
  `TransformKind`, coarse/global, schedule, non-rigid) is owned by the follow-up
  slices enumerated in
  [`docs/architecture/geometry-pipeline-modularity.md`](../../../docs/architecture/geometry-pipeline-modularity.md)
  §8; those `GEOM-0NN`/`METHOD-*` tasks are opened as each becomes the priority.
