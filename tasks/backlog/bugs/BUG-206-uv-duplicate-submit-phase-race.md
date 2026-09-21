---
id: BUG-206
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: discovered during interactive reuse-task verification; evidence is retained test output
contract_schema: 1
contracts: []
contract_review: Catalog reviewed; this is a test scheduling and observation defect, with no proposed geometry binding, publication, public interface or dependency change.
---
# BUG-206 — Stabilize duplicate UV-submit diagnostic phase observation

## Goal
Make the duplicate-submission regression deterministic while retaining its
single-job, one-delivery, no-second-sink and invalid-topology bypass assertions.

## Evidence
Observed during GEOM-099's fresh Clang 23 UBSan CPU gate on 2026-09-21.
`SandboxEditorUi.UvRegenerationDuplicateSubmitUsesExistingActiveJob` fails at
`tests/contract/runtime/Test.SandboxEditorMeshMethods.cpp:5574`: preview says
`active queued job (job 0:1)` and duplicate apply says
`active running job (job 0:1)`. See the [retained output](../../evidence/BUG-206/ubsan-failure.log).
There is no UBSan runtime diagnostic in the failure. Both observations identify
the same active job, but a two-worker scheduler can advance its phase between
preview and apply. This test and the job-admission code are unchanged from
`7bdffefb309d9a3fe1815e371948a899b4a975a1`; the same test passed in this session's
ci and ASan gates. An unchanged-source UBSan repetition passed three times and failed on the fourth
with the same queued/running mismatch; see [repeat output](../../evidence/BUG-206/ubsan-repeat.log).
The original topology implementation then reproduced the same mismatch on its
first UBSan repetition; see [baseline output](../../evidence/BUG-206/baseline-ubsan-repeat.log).
Diagnostic source `e8d0cf16cfdc48589ce08a58a5f00ffe1deab248` is byte-identical
to measured before source `14af21fbaf3590cb96df7f478fd94247224facf3` throughout
`src`, `tests` and `cmake`; it was built with the unchanged ci-ubsan configuration.
The reviewed GEOM-099 implementation was subsequently restored and rebuilt.
This confirms that the failure does not require the extraction. UI-037 owns
the related readiness work.

The four reuse tasks do not authorize an unrelated lifecycle rewrite. This
record preserves the newly observed failure; no assertion or label is weakened.

The subsequent GEOIO-004 full unsanitized ci run also reproduced exactly the
same queued/running comparison for job `0:1`. Of 4,886 selected tests, this
was the only failure, alongside the expected GLFW skip. See [ci evidence](../../evidence/BUG-206/ci-phase-race.log).
Thus this race is not specific to UBSan. The PLY extraction's 224 focused
GeometryIO cases and expanded 319-case locality selection pass.

## Acceptance criteria
- [ ] Reproduce the phase-observation race and pin the intended preview/apply contract.
- [ ] Control worker progress for exact phase-string equality, or compare stable
      duplicate-job identity under the documented observation contract, while
      retaining both phase diagnostics and every existing submission/delivery assertion.
- [ ] Pass repeated focused ci/ASan/UBSan runs and the full CPU sanitizer selectors.

## Verification
```bash
cmake --preset ci-ubsan --fresh -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci-ubsan --output-on-failure -R '^SandboxEditorUi.UvRegenerationDuplicateSubmitUsesExistingActiveJob$' --repeat until-fail:100 --timeout 60
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
```
