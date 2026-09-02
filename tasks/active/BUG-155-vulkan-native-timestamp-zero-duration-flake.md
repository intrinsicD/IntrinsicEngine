---
id: BUG-155
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-root"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-02T18:54:58+02:00"
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation]
contract_review: "The correction clarifies the existing native timestamp value contract: an available ordered pair may be zero, while unavailable and incoherent pairs remain fail-closed. It adds bounded Vulkan diagnostics and tightens the gpu;vulkan proof around exact raw-pair evidence without changing RHI types, layer edges, frame-slot ownership, backend selection, or performance claims."
maturity_target: Operational
---
# BUG-155 — Native Vulkan timestamp smoke intermittently publishes zero duration

## Status

- Complete on `main` and ready for retirement after the repository-contract
  gates. The defect is diagnosed, the named smoke is corrected, and two
  complete `gpu;vulkan` cohorts passed without retries.
- On NVIDIA GeForce RTX 3050, driver 590.48.01, the unchanged smoke reproduced
  on isolated attempt 4 after three passes. The diagnostic build reproduced on
  attempt 5 after four passes and captured the exact `SurfacePass` pair:
  frame 4/slot 1, queries 1036/1037, both availability values 1, 64 valid bits,
  1 ns advertised period, equal raw ticks, zero delta, and renderer status
  `Recorded`.
- The corrected smoke passed 20/20 repetitions. Ten additional XML-recorded
  attempts passed with nine 1024 ns samples and one legal zero sample whose
  raw evidence satisfied the exact-frame/slot/pass checks.

## Goal

- Determine why a recorded native-GPU `SurfacePass` can intermittently publish
  a present but zero-nanosecond duration after query-slot reuse, then correct
  the profiler or its operational proof without masking the failure.

## Non-goals

- No retry-until-green wrapper, flaky quarantine, assertion weakening, or
  unconditional duration clamping.
- No curvature, geometry-property, OBJ-import, or runtime normal-path change.
- No GPU performance claim from one host-local smoke test.

## Context

- During final evidence refresh for `BUG-154` on 2026-08-12, the promoted
  Vulkan ASan+UBSan cohort first passed 54/54. A second identical 54-case run
  failed only
  `DefaultRecipeSurfaceGpuSmoke.NativeGpuTimestampsResolveNamedPassesAfterSlotReuse`:
  `SurfacePass::DurationNs` was present but equal to zero at
  `Test.DefaultRecipeSurfaceGpuSmoke.cpp:886`; the other 53 cases, including
  validation and the 93-second Vulkan shutdown/LeakSanitizer contract, passed.
- Three immediate isolated repetitions of the exact failing case then passed
  in 2.93–2.96 seconds. The failure is therefore intermittent and is preserved
  in `tasks/evidence/BUG-154/commands/final-ci-vulkan.*`; passing retries do not
  erase it.
- The failing binary and source surface were unchanged between the green full
  run, failed full rerun, and three green isolated reruns. The `BUG-154`
  curvature/smoothing implementation does not own RHI timestamp queries.
- Ranked hypotheses:
  1. A valid start/end pair can quantize to the same device timestamp tick for
     a short pass, so the test's strict-positive contract exceeds the
     advertised timestamp granularity.
  2. Slot reuse occasionally resolves a stale, partially available, or
     mismatched query pair while still marking the row `NativeGpu` and fresh.
  3. A reset/record/availability ordering race permits both query values to be
     identical on one submission.
  4. Driver/device scheduling occasionally produces identical raw ticks; if
     so, diagnostics must distinguish a valid zero interval from missing or
     incoherent query data rather than clamping it silently.
- The 2026-09-02 capture supports hypotheses 1 and 4: the device exposed a
  coherent, available, correctly indexed equal-tick pair, and neighboring
  recorded passes plus the graphics envelope advanced in 1024 ns increments.
  Hypothesis 2 is rejected because the raw delta was exactly zero rather than
  a positive sub-nanosecond conversion. Hypothesis 3 is rejected by the exact
  reused frame/slot/query mapping, both availability values, coherent adjacent
  rows, and unchanged validation counters. Vulkan's normative ordering rule
  requires later writes to be non-decreasing, not strictly increasing.

## Required changes

- [x] Add bounded diagnostic capture of raw start/end ticks, valid-bit mask,
      timestamp period, resolved slot/submission, availability values, and
      command status whenever a native row resolves to zero duration.
- [x] Reproduce under repeated isolated and full-cohort runs on the same
      Vulkan device, preserving every failure and reporting a distribution
      rather than accepting a passing retry.
- [x] Decide from evidence whether zero is a legal quantized interval or a
      query lifecycle defect; fix the profiler publication contract or the
      reset/record/resolve ordering accordingly.
- [x] Keep unsupported, unavailable, stale, and valid-native-zero states
      semantically distinct and fail closed on incoherent query pairs.

## Tests

- [x] Add a deterministic profiler-level regression for identical raw ticks
      and for stale/partially available query pairs.
- [x] Run the named smoke repeatedly across at least two complete query-slot
      reuse windows per attempt and retain raw-query diagnostics.
- [x] Pass the complete `gpu;vulkan` intersection repeatedly without retries,
      quarantine, exclusions, or weakened validation/LeakSanitizer coverage.

## Docs

- [x] Update the native timestamp contract in `tests/README.md` and the owning
      RHI/renderer documentation if the legal zero-duration semantics change.
- [x] Record the device/driver identity, reproduction distribution, selected
      correction, and rejected hypotheses in this task before retirement.

## Acceptance criteria

- [x] The intermittent zero-duration result has a deterministic explanation
      backed by raw query and slot-reuse evidence.
- [x] Valid, unavailable, stale, and incoherent native timestamp results are
      published distinctly without fabricated positive durations.
- [x] Repeated named-smoke and complete promoted-Vulkan cohorts pass under the
      documented device capabilities and budgets.
- [x] No retry, quarantine, timeout weakening, or unrelated production change
      is used to reach green.

## Verification

```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure \
  -R '^DefaultRecipeSurfaceGpuSmoke.NativeGpuTimestampsResolveNamedPassesAfterSlotReuse$' \
  --repeat until-fail:20 --timeout 120
ctest --test-dir build/ci-vulkan --output-on-failure \
  -L 'gpu' -L 'vulkan' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

Evidence recorded on 2026-09-02:

- Fresh `ci-vulkan` configure plus `IntrinsicTests` build: pass, 2,235
  build steps.
- Unchanged named baseline: three passes then the preserved zero failure on
  attempt 4 (14.34 seconds).
- Diagnostic named baseline: four passes then the preserved zero failure on
  attempt 5 (15.50 seconds); raw evidence is summarized in `## Status`.
- `RHIProfiler.DurationResolutionPreservesAvailableZeroIntervals`: 1/1 pass.
  Existing `DurationResolutionRequiresBothAvailableValues` and
  `FrameIdentityRetainsOtherSlotResultAndRetiresReusedSlot` cover partial
  availability and stale reused-frame identity.
- Corrected named smoke: 20/20 passes (134.95 seconds); ten additional
  XML-recorded attempts passed with a 9 positive / 1 legal-zero distribution.
- Complete promoted-Vulkan cohorts: 54/54 passed in 385.39 seconds, then 54/54
  passed in 326.83 seconds. Both included validation-bearing smokes and the
  Vulkan shutdown/LeakSanitizer contract; neither used a retry or exclusion.

## Forbidden changes

- Retrying a failed required gate until only a green log remains.
- Treating one passing rerun as proof that the defect is fixed.
- Clamping zero to one nanosecond without proving that the raw pair is valid.
- Mixing this profiler diagnosis into `BUG-154`'s geometry correction.

## Maturity

- Target: restore the existing native timestamp profiler's `Operational`
  evidence on Vulkan-capable hosts; no new backend or profiler feature is
  introduced.
