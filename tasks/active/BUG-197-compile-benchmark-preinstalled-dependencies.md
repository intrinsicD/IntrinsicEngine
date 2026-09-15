---
id: BUG-197
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Benchmark harness correctness; preserve the failed cohort and verify dependency immutability before comparisons.
contract_schema: 1
contracts: []
contract_review: AGENTS.md benchmark source and dependency identity requirements apply; the catalog has no dedicated build-benchmark dependency contract.
---
# BUG-197 — Compile benchmark can reinstall borrowed dependencies

## Goal
Keep preinstalled dependency state immutable while measuring engine compilation
in a disposable worktree, and detect mutation before consuming invalid timings.

## Evidence
- RUNTIME-265's first measurement used a separate worktree and build directory,
  sharing the preinstalled dependency directory. Its initial configure restored
  18 packages from vcpkg's binary cache because worktree/toolchain/overlay paths
  changed the package identity. The runner caught a changed dependency digest
  only before sample two. The incomplete cohort is excluded from comparisons.
- Raw attempt: `/tmp/intrinsic-runtime265-20260915/measurement`; the configure log
  names the restored packages and the runner log records the digest failure.

## Acceptance criteria
- [x] Force package installation off in measured configure commands; packages
      must already exist and remain borrowed from their current owner.
- [x] Reuse one content/path/membership/link/permission snapshot for dependency pre-read and
      checks before sampling, after configure, and after measured builds.
- [x] Regression tests detect missing trees, content changes with unchanged
      size, path changes, membership changes, symlink targets and permissions.
- [ ] Claude reviews the correction and a fresh complete matched cohort passes
      with one dependency digest throughout; retain the rejected attempt.
- [ ] Restore and reconcile the canonical ci dependency/build state after the
      accidental reinstall, then retire with exact validation evidence.

## Verification
```bash
python3 tests/regression/tooling/Test.CompileHotspots.py
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/analysis/benchmark_compile_iteration.py --source /tmp/intrinsic-runtime265-measure --build /dev/shm/intrinsic-runtime265-build --output /tmp/intrinsic-runtime265-20260915/measurement-fixed --manifest benchmarks/ci/manifests/engine_compile_iteration_service_borrows.yaml
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -j 4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Review resolution
- Claude reviewed the correction. Actual configure argv is already preserved in
  every `initial-configure.execution.json`; the forced install-off option is
  therefore auditable. Missing packages remain a normal fail-closed CMake error.
- Added link-target and permission identity, including dangling links; reject
  directory symlinks rather than silently omit their contents. Empty directories
  are irrelevant to compiler inputs and are not fingerprinted.
- Fingerprint checks and input pre-read occur outside the timed subprocess.
  Cache state is intentionally warm-input, not a cold-filesystem claim.
- Restored the canonical ci configuration before the new cohort. Its dependency
  snapshot is preserved separately and must match every new sample. No known-good
  package digest existed before the failed attempt, so pinning a newly invented
  manifest digest would add no independent correctness evidence; the full CPU
  reconciliation verifies the restored tree.
- Regression suite: 26 tests passed. The previous incomplete cohort remains
  excluded; the complete ABBA run restarts under `measurement-fixed`.
