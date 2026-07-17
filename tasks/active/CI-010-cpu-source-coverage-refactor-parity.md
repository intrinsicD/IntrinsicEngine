---
id: CI-010
theme: H
depends_on:
  - CI-004
  - BUG-106
  - BUG-107
---
# CI-010 — Establish CPU source-coverage refactor parity

## Status
- In progress on 2026-07-17; owner: Codex; branch: `main`.
- Next verification: add the instrumented preset and fail-closed coverage
  collector/comparator, then validate the two-executable synthetic fixture
  before dispatching one complete hosted baseline.

## Goal
- Produce a reproducible Clang source-coverage baseline for the canonical CPU
  test cohort and a fail-closed comparison mode for test-only refactors.

## Non-goals
- No replacement of assertion-level correctness, Vulkan smokes, shader checks,
  sanitizers, or the full CPU gate with a coverage percentage.
- No required global or diff-coverage percentage threshold in this first task;
  threshold policy waits for stable exclusions and repeated baselines.
- No claim that CPU source coverage proves GPU execution or behavioral
  invariant quality.
- No third-party coverage service or new orchestration framework unless the
  repository-native Clang tools prove insufficient.

## Context
- Owner: CMake presets, test registry, coverage collection tooling, and one
  dedicated CI/reporting workflow.
- The repository currently has no `-fprofile-instr-generate`,
  `-fcoverage-mapping`, `llvm-profdata`, or `llvm-cov` preset/workflow. Test
  refactors can therefore compare case inventories but not covered line,
  branch, or region sets.
- Clang emits profiles per process; parallel CTest and multiple executables
  require collision-safe `%m-%p.profraw` paths and one merged profile.
- `llvm-cov` needs coverage mappings from every relevant executable, not only
  one umbrella or representative binary. Retired `CI-004` supplies the
  canonical executable/label registry after `BUG-106` restores truthful
  ownership and `BUG-107` stabilizes the configured graph.
- A test-only parity comparison is valid only when production sources, compiler,
  flags, preset identity, and exclusion rules are identical on both sides.

## Required changes
- [ ] Add an unsanitized `ci-coverage-cpu` configure/build preset with tests
      enabled, Sandbox/benchmarks/CUDA disabled, explicit backend identity, and
      Clang source-coverage compile/link flags.
- [ ] Derive the selected CPU executable list from the canonical generated test
      registry and fail if an expected binary is missing, uninstrumented, or
      omitted from the `llvm-cov` object set.
- [ ] Run the canonical CPU selector with collision-safe raw-profile paths,
      merge every shard with `llvm-profdata`, and retain missing/corrupt-profile
      diagnostics.
- [ ] Export machine-readable line, branch, function, and region coverage plus
      the exact unique GoogleTest name/label/executable inventory.
- [ ] Define and document engine-owned production roots and deterministic
      exclusions for tests, generated files, vcpkg/external code, compiler
      runtime code, and other non-product inputs.
- [ ] Add a comparison mode for identical-production-commit test refactors that
      normalizes paths and fails when a previously covered production region or
      branch disappears, independent of the aggregate percentage.
- [ ] Emit informational diff coverage for changed production lines without
      making a permanent threshold required until baseline stability is
      reviewed under `CI-009`.
- [ ] Publish the report and raw/merged profile diagnostics from a dedicated
      default-branch, scheduled, or manually triggered job; final lifecycle
      placement remains `CI-009`.

## Tests
- [ ] Add synthetic tooling regressions with at least two instrumented
      executables and parallel profile writes, proving complete object/profile
      aggregation.
- [ ] Prove missing executable mappings, zero selected tests, corrupt profiles,
      production-source drift in parity mode, and a lost covered region fail
      nonzero with actionable diagnostics.
- [ ] Prove normalized path/exclusion handling is deterministic across two
      build directories.
- [ ] Capture one named baseline from the complete selected CPU cohort and
      validate its JSON/export artifacts.

## Docs
- [ ] Add the CPU source-coverage policy to
      `docs/benchmarking/ci-policy.md`, distinguishing test inventory, source
      regions, assertions/invariants, and backend-specific evidence.
- [ ] Document local reproduction, included/excluded roots, comparison
      preconditions, artifact schema, and why a percentage alone is not a
      no-coverage-loss proof.
- [ ] Update process/task indexes and regenerate `tasks/SESSION-BRIEF.md` on
      retirement.

## Acceptance criteria
- [ ] One command sequence produces a complete CPU test inventory, merged
      profile, and line/branch/region export using every registered CPU binary.
- [ ] Parallel shards cannot overwrite one another and missing mappings or
      profiles cannot yield a success-shaped partial report.
- [ ] On an identical production commit, removing execution of a previously
      covered region causes the test-refactor parity check to fail even if the
      global percentage stays equal or rises.
- [ ] The baseline records compiler/preset/backend identity and deterministic
      exclusions so later results are comparable.
- [ ] Existing correctness, sanitizer, and GPU gates remain authoritative and
      unchanged.

## Verification
```bash
cmake --preset ci-coverage-cpu --fresh
cmake --build --preset ci-coverage-cpu --target IntrinsicCpuTests
python3 tools/ci/run_source_coverage.py --build-dir build/ci-coverage-cpu --output build/ci-coverage-cpu/coverage
python3 tests/regression/tooling/Test.SourceCoverage.py
python3 tools/ci/compare_source_coverage.py --baseline <baseline.json> --candidate <candidate.json> --test-only-refactor
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Reporting engine-wide coverage from only `IntrinsicCoreTests` or another
  representative executable.
- Treating equal aggregate percentages or equal test counts as region parity.
- Hiding an uninstrumented/missing binary with a warning-only path.
- Uploading configured build trees or BMIs as cross-job artifacts.
