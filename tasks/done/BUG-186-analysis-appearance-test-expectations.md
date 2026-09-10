---
id: BUG-186
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive test-only correction with before/after CTest evidence
contract_schema: 1
contracts: []
contract_review: Test assertions are synchronized with existing appearance behavior; geometry, method eligibility, publication, property coherence and runtime config contracts are unchanged.
---
# BUG-186 — Analysis tests expect retired recipe storage

## Goal
Correct four stale analysis-display assertions left behind by the appearance unification in `2c9053f89`.

## Evidence and diagnosis
The RUNTIME-230 full CPU run selected 4501 cases and found four existing tests failing at `ASSERT_TRUE(stored)`: outlier mask/score, kernel density, keypoints and point spacing. That commit routes property-only scalar/label requests through `VisualizationLaneOverrides`, and already updated the descriptor test accordingly. These four test files and the visualization command implementation were unchanged by RUNTIME-230. The new construction menu's separate count assertion is owned by RUNTIME-230.

Keep all eight analysis domains and numerical/history checks. Inspect the selected appearance lane and property name on supported domains, require the recipe store to remain unused, and require the existing explicit invalid-property response for halfedge appearance. This does not change the methods or add halfedge rendering.

## Acceptance criteria
- [x] All four tests check current appearance state and existing halfedge rejection without removing numerical or history assertions.
- [x] All four appearance cases and the full CPU selector pass, with the existing unsanitized leak-control skip identified separately.

## Verification
```bash
cmake --preset ci -DINTRINSIC_BUILD_SANDBOX=ON
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox -j 6
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Verification outcome
The final full CPU run passes all four cases: 4500 total passes, zero failures and one expected unsanitized leak-control skip. Initial and final compressed CTest logs are bound by [the source record](../../ara/evidence/diagnostics/point_construction_2026-09-10/record.json). No production visualization or method code changed for this correction.

## Completion
Completed 2026-09-10. Implementation commit: `27275206dd6d3d1dd6857db737f3d609ab4cb1cb`.
