---
id: BUG-155
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "This backlog diagnosis does not yet change a reusable engine contract. If evidence requires changing native timestamp publication, frame-slot reuse, or the gpu;vulkan test policy, the implementing slice must declare the applicable rendering or repository contract before it is claimed."
---
# BUG-155 — Native Vulkan timestamp smoke intermittently publishes zero duration

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

## Required changes

- [ ] Add bounded diagnostic capture of raw start/end ticks, valid-bit mask,
      timestamp period, resolved slot/submission, availability values, and
      command status whenever a native row resolves to zero duration.
- [ ] Reproduce under repeated isolated and full-cohort runs on the same
      Vulkan device, preserving every failure and reporting a distribution
      rather than accepting a passing retry.
- [ ] Decide from evidence whether zero is a legal quantized interval or a
      query lifecycle defect; fix the profiler publication contract or the
      reset/record/resolve ordering accordingly.
- [ ] Keep unsupported, unavailable, stale, and valid-native-zero states
      semantically distinct and fail closed on incoherent query pairs.

## Tests

- [ ] Add a deterministic profiler-level regression for identical raw ticks
      and for stale/partially available query pairs.
- [ ] Run the named smoke repeatedly across at least two complete query-slot
      reuse windows per attempt and retain raw-query diagnostics.
- [ ] Pass the complete `gpu;vulkan` intersection repeatedly without retries,
      quarantine, exclusions, or weakened validation/LeakSanitizer coverage.

## Docs

- [ ] Update the native timestamp contract in `tests/README.md` and the owning
      RHI/renderer documentation if the legal zero-duration semantics change.
- [ ] Record the device/driver identity, reproduction distribution, selected
      correction, and rejected hypotheses in this task before retirement.

## Acceptance criteria

- [ ] The intermittent zero-duration result has a deterministic explanation
      backed by raw query and slot-reuse evidence.
- [ ] Valid, unavailable, stale, and incoherent native timestamp results are
      published distinctly without fabricated positive durations.
- [ ] Repeated named-smoke and complete promoted-Vulkan cohorts pass under the
      documented device capabilities and budgets.
- [ ] No retry, quarantine, timeout weakening, or unrelated production change
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

## Forbidden changes

- Retrying a failed required gate until only a green log remains.
- Treating one passing rerun as proof that the defect is fixed.
- Clamping zero to one nanosecond without proving that the raw pair is valid.
- Mixing this profiler diagnosis into `BUG-154`'s geometry correction.

## Maturity

- Target: restore the existing native timestamp profiler's `Operational`
  evidence on Vulkan-capable hosts; no new backend or profiler feature is
  introduced.
