---
id: BUG-124
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude-bug124"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-08T09:15:56Z"
contract_schema: 1
contracts: []
contract_review: >-
  Reviewed the catalog; this repair is confined to one GPU smoke fixture and
  its assertions. It publishes nothing to an ECS geometry source and changes no
  element-domain property, adjacency, support-radius, parameterization, or
  method-integration surface. The `v:displacement` vertex property it seeds
  exists only inside the test scene, and no production code changed, so no
  reusable task-workflow contract is created or consumed.
---
# BUG-124 — Geometry-presentation GPU smoke expects a retired unsupported slot

## Status

- Completed and retired on 2026-08-08.
- The assertion was stale, but the **contract was not retired** — only the
  fixture's one qualifying combination was. `RUNTIME-198` (`6171fad6`) narrowed
  the unsupported rule in `BuildGeometryPresentationSnapshot` from "any
  property-buffer slot on a surface material" to "any such slot whose semantic
  is not `ScalarField`", because scalar fields became backend-resident. The
  mesh fixture's only property-buffer surface slot was `f:heat`, a
  `ScalarField`, so after that narrowing the snapshot legitimately reported
  `UnsupportedSlotCount == 0` and the `>= 1` assertions failed.
- A surface material fed by a property buffer through a **non**-scalar-field
  semantic is still unsupported today, and this smoke is the only coverage of
  that path anywhere in the tree. The fixture therefore now declares a
  `Displacement` slot sourced from a `v:displacement` vertex property buffer,
  which is exactly that combination, and the seeded mesh carries the backing
  property so the slot resolves and is flagged unsupported rather than failing
  to resolve.
- The two counter assertions were replaced by
  `ExpectSurfaceDisplacementUnsupported`, which locates that named slot and
  asserts `Unsupported` on it before checking the aggregate counter. A bare
  `>= 1` counter cannot distinguish "the contract retired this combination"
  from "the fixture stopped declaring one" — the precise ambiguity that let
  this bug sit. A future narrowing of the rule now fails with the slot's
  semantic, readiness, and diagnostic in the message.
- `Displacement` projects to no visualization recipe in
  `Runtime.RenderExtraction.cpp`, so the extra slot changes no lane, upload, or
  pack behaviour; the extraction-side
  `GeometryPresentationUnsupportedSlotCount` assertion, previously satisfied
  vacuously by `>= 0`, now carries a real value.
- No production code changed. No label, timeout, or assertion was weakened.
- Verified on an RTX 3050 (driver 590.48.01) under the combined ASan+UBSan
  `ci-vulkan` preset: the smoke passed five consecutive repetitions and the
  full `-L gpu -L vulkan` intersection passed 53/53. No new performance or
  parity claim is made.
- Completion commit: this retirement commit.

## Goal
- Restore the full promoted-Vulkan gate by making the geometry-presentation
  smoke fixture and its unsupported-slot assertions describe the same current
  contract.

## Non-goals
- Weakening Vulkan execution, visibility, presentation-readiness, or render
  extraction assertions.
- Changing asset-import or texture-residency behavior.

## Context
- Symptom: the 2026-07-31 RUNTIME-200 full `gpu;vulkan` gate passed 47/48
  cases, but
  `RuntimeSandboxAcceptanceGpuSmoke.GeometryPresentationReachesOperationalFrame`
  failed because both snapshots reported `UnsupportedSlotCount == 0` while the
  test expected at least one unsupported slot. The complete failure is retained
  in
  `tasks/evidence/RUNTIME-200/commands/ci-vulkan-full-gate.stdout.log`.
- Expected behavior: either seed an explicitly unsupported current
  presentation combination and keep the counter coverage, or update the
  assertions if zero unsupported slots is now the intended fixture contract.
- Impact: the full promoted-Vulkan gate is red even though all 47 neighboring
  cases, including all four import/model-scene smokes, pass.
- The production `Runtime.GeometryPresentation.cpp` surface and the failing
  assertions are unchanged from `origin/main`; this is not caused by the
  RUNTIME-200 import-workflow cleanup.

## Required changes
- [x] Identify which current presentation combination the fixture intends to
      exercise as unsupported.
- [x] Align the fixture and initial/ready/extraction counter assertions without
      deleting unsupported-path coverage unless the contract is explicitly
      retired.

## Tests
- [x] The exact failing GPU smoke passes at least five consecutive repetitions.
- [x] The full `gpu;vulkan` intersection passes.

## Docs
- [x] Record the resolved current contract in this task and the retirement log;
      update runtime presentation docs only if behavior changes. Behavior did
      not change, so no runtime presentation doc was touched.

## Acceptance criteria
- [x] The smoke asserts a real current geometry-presentation contract rather
      than a stale counter value.
- [x] No GPU/Vulkan label, timeout, or assertion is weakened to hide the
      failure.
- [x] The fix introduces no layering violation.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R '^RuntimeSandboxAcceptanceGpuSmoke\.GeometryPresentationReachesOperationalFrame$' \
  --repeat until-fail:5 --timeout 120
ctest --test-dir build/ci-vulkan --output-on-failure \
  -L gpu -L vulkan --timeout 120
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Shipping a zero-counter assertion without deciding whether unsupported-path
  coverage still belongs in this operational smoke.
- Quarantining or relabeling the failing test.
