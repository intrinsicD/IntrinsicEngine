---
id: BUG-124
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
---
# BUG-124 — Geometry-presentation GPU smoke expects a retired unsupported slot

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
- [ ] Identify which current presentation combination the fixture intends to
      exercise as unsupported.
- [ ] Align the fixture and initial/ready/extraction counter assertions without
      deleting unsupported-path coverage unless the contract is explicitly
      retired.

## Tests
- [ ] The exact failing GPU smoke passes at least five consecutive repetitions.
- [ ] The full `gpu;vulkan` intersection passes.

## Docs
- [ ] Record the resolved current contract in this task and the retirement log;
      update runtime presentation docs only if behavior changes.

## Acceptance criteria
- [ ] The smoke asserts a real current geometry-presentation contract rather
      than a stale counter value.
- [ ] No GPU/Vulkan label, timeout, or assertion is weakened to hide the
      failure.
- [ ] The fix introduces no layering violation.

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
