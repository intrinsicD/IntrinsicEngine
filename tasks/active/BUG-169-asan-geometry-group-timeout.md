---
id: BUG-169
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "interactive CI reliability repair; evidence is exact logical registration parity plus local and hosted sanitizer execution"
owner: Codex
branch: codex/bug-166-historical-input-seals
worktree: /tmp/intrinsic-bug166
claimed_at: "2026-09-05T04:19:12+02:00"
contract_schema: 1
contracts: []
contract_review: "Reviewed the catalog; this test-topology repair preserves the existing grouped-test, sanitizer, geometry, timeout, and label contracts and changes no reusable engine or workflow contract."
---
# BUG-169 — ASan geometry group exceeds its fixed process timeout

## Goal
- Restore the required hosted ASan gate after cumulative geometry-suite growth
  pushed one otherwise healthy grouped process past its fixed 120-second
  hang-detection budget.

## Acceptance criteria
- [x] The fixture-heavy curvature tensor and segmentation cases run in a
      dedicated pure GoogleTest producer while remaining in every default CPU,
      ASan, UBSan, and focused geometry selection that previously covered them.
- [x] Grouped and individually discovered plans retain exact logical-case and
      execution parity, with one canonical 120-second wrapper per pure producer.
- [ ] Both geometry wrappers complete below 120 seconds under local ASan and
      the exact hosted ASan gate passes without a timeout or coverage exclusion.

## Verification
```bash
python3 tests/regression/tooling/Test.TestGateRouting.py --self-test
python3 tests/regression/tooling/Test.TouchedScope.py -v
cmake --preset ci -B build/ci-grouped --fresh \
  -DINTRINSIC_GROUP_PURE_CTEST=ON
cp -al build/ci/bin/. build/ci-grouped/bin/
python3 tests/regression/tooling/Test.GroupedCTestParity.py registration \
  --individual-build-dir build/ci \
  --grouped-build-dir build/ci-grouped \
  --output /tmp/bug169-grouped-registration.json
ctest --test-dir build/ci -Q \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 --parallel 4 \
  --output-junit /tmp/bug169-individual.junit.xml
ctest --test-dir build/ci-grouped -Q \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 --parallel 4 \
  --output-junit /tmp/bug169-grouped-unsanitized.junit.xml
python3 tests/regression/tooling/Test.GroupedCTestParity.py execution \
  --individual-build-dir build/ci \
  --grouped-build-dir build/ci-grouped \
  --individual-junit /tmp/bug169-individual.junit.xml \
  --grouped-junit /tmp/bug169-grouped-unsanitized.junit.xml \
  --grouped-gtest-dir build/ci-grouped/reports/grouped-ctest/gtest \
  --output /tmp/bug169-grouped-execution.json
ctest --test-dir build/ci-asan --output-on-failure \
  -R '^IntrinsicGeometry(Curvature)?Tests.Grouped$' --timeout 60 --parallel 1
# Hosted evidence: ci-linux-clang / asan concludes green.
```

## Context
- PR #1037 run `33936262942`, job `101224744164`, passed 2,747 physical
  tests and timed out only `IntrinsicGeometryTests.Grouped` after 120.051
  seconds. Its GoogleTest output had reached
  `Registration_ICP.RecoversRigidTransform_PointToPlane`; this was cumulative
  runtime, not a hang in that final case.
- The same hosted source surface passed all 4,263 CPU-supported logical tests
  and UBSan. The UBSan geometry wrapper completed in 49.751 seconds.
- The newly added
  `CurvatureTensor.SculptAssetProducesStableFeatureAlignedParts` case consumed
  40.938 seconds under hosted ASan. Prior passing hosted ASan geometry wrappers
  were 81.59 and 82.22 seconds, so retaining one process no longer leaves a
  safe runner-variance margin.
- The unchanged 1,417-case geometry binary completed locally under ASan in
  88.36 seconds. That rules out a deterministic deadlock and supports a
  workload-partition repair rather than weakening the timeout or test set.
- The split local ASan wrappers pass in 55.94 and 27.59 seconds; the full
  serial ASan run repeated them in 56.23 and 27.78 seconds. Its only failure
  was the pre-existing `BUG-082` synthetic LeakSanitizer control timing out on
  this Clang 23 host after printing its allocation marker; the hosted Clang 20
  gate owns the required clean result.
- Canonical same-binary individual/grouped registration and execution parity
  passes for all 4,262 GoogleTest cases with logical digest
  `0cead2731d00705a42ea9574fa5ad1208a90c452a71609ebde38d7709d7321ec`;
  all cases pass in both plans, and the one unchanged GLFW capability test
  self-skips in both.
