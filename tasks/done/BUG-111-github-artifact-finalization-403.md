---
id: BUG-111
theme: G
depends_on: []
---
# BUG-111 — GitHub artifact finalization can discard passing CI evidence

## Status
- Completed on 2026-07-17; owner: Codex; branch: `main`.
- Tracking commit: `44da41e3`.
- GitHub's specific-job rerun retained the original SHA/ref and reran only the
  failed ASan matrix child. Attempt 2 job `87928232616` passed and finalized
  timing artifact `8412801543` plus selection artifact `8412802000`.

## Goal
- Determine whether hosted artifact finalization failures need a repository
  mitigation or an explicit bounded rerun policy, without treating a passing
  test process as complete when required evidence is unavailable.

## Non-goals
- No suppression of artifact upload failures.
- No weakening of sanitizer test-selection parity when an input artifact is
  missing.
- No broad retry wrapper around compilation or test execution without measured
  failure frequency.

## Context
- Symptom: `ci-linux-clang` run `29589810886`, ASan job `87915791947`,
  passed 4,062/4,062 selected CPU tests and generated a validated passing gate
  timing result, then `actions/upload-artifact@v4` failed while finalizing
  `ci-gate-timing-ci-sanitizers-asan` with intermediary HTTP `403 Forbidden`.
  The immediately following selection-artifact upload succeeded.
- Expected behavior: required timing and selection artifacts finalize
  reliably, or a bounded evidence-preserving recovery path reports the
  platform failure without rerunning unrelated variants.
- Impact: the sanitizer job and three-way parity workflow fail closed despite
  successful compilation and tests; the affected run cannot enter a
  claim-grade timing population.

## Required changes
- [x] Collect the action version, runner image, artifact name/size, run/job IDs,
      and service response for every recurrence; distinguish a platform
      incident from deterministic repository input.
- [x] Evaluate the smallest supported recovery mechanism, including an
      artifact-only retry when GitHub Actions exposes one; do not rerun all
      sanitizer variants by default.
- [x] If no repository mitigation is reliable, document a bounded failed-job
      rerun policy and the evidence needed to close this as external.
- [x] Preserve fail-closed selection parity whenever any required artifact is
      absent or unvalidated.

## Tests
- [x] Add workflow-contract coverage for any repository-owned retry or
      diagnostic path; an external-only disposition records why a hermetic
      failure injection is not representative.

## Docs
- [x] Update CI policy with the final recovery/disposition and retained
      incident evidence.

## Acceptance criteria
- [x] The incident has a reproducible repository trigger or an evidence-backed
      external-service disposition.
- [x] Required artifacts and parity cannot silently succeed when finalization
      fails.
- [x] Any retry is bounded to the smallest failed work and cannot duplicate
      unrelated sanitizer or CPU builds.

## Disposition
- Attempt 1 ASan job `87915791947` passed 4,062/4,062 tests and validated a
  passing result. `actions/upload-artifact@v4` then received intermediary HTTP
  403 while finalizing the 954-byte timing artifact; the following 73,649-byte
  selection artifact finalized successfully.
- `gh run rerun --job 87915791947` created attempt 2 job `87928232616` at the
  same `a7ae8e7f` SHA. It passed the same selector and finalized both artifacts.
  The retained UBSan and unsanitized dependency records kept their original
  timestamps, proving they were not rerun.
- The one-action failure followed by sibling and exact-rerun success provides
  no deterministic repository trigger. A synthetic HTTP failure would test a
  different path than GitHub's managed artifact finalization, so no hermetic
  repository regression was added.
- Required jobs remain fail closed. Recover a matching incident with
  `gh run rerun --job <failed-job-id>`; do not use a whole-run or whole-matrix
  retry unless additional jobs actually failed.

## Verification
```bash
gh run view 29589810886 --job 87915791947 --log-failed
python3 tests/regression/tooling/Test.SanitizerPresets.py
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Marking the failed run as passing because its test process passed.
- Disabling timing or selection artifact uploads.
- Rebuilding every sanitizer variant to recover one artifact finalization.
