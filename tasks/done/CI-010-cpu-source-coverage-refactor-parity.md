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
- Completed on 2026-07-17 at `Operational`; owner: Codex; branch: `main`.
- Commit reference: `b9f74774` added the canonical collector, workflow,
  preset, policy, and regression suite; `0d26bbaf` corrected the CTest selector;
  `67adc536` made test-target splits parity-safe while retaining exact
  case-working-directory identity.
- Exact-head hosted run `29575099005` at
  `67adc53614e14a8ca59c656265103f04f1e69ec4` passed the complete v2 workflow.
  Coverage artifact `8405268613` and timing artifact `8405267687` retain the
  claim-grade outputs.
- The baseline reconciles 26 targets, 4,062 CTest records, 50 discovery
  profiles, 26 execution profiles, 25 GoogleTest XML reports, and 26 coverage
  objects. All 4,061 enabled GoogleTest cases passed; the one manual CTest
  producer emitted its required profile and object.
- Schema-v2 execution identity covers all 4,062 case working directories.
  Self-parity lost zero covered production regions and zero branch arms.

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
- [x] Add an unsanitized `ci-coverage-cpu` configure/build preset with tests
      enabled, Sandbox/benchmarks/CUDA disabled, explicit backend identity, and
      Clang source-coverage compile/link flags.
- [x] Derive the selected CPU executable list from the canonical generated test
      registry and fail if an expected binary is missing, uninstrumented, or
      omitted from the `llvm-cov` object set.
- [x] Run the canonical CPU selector with collision-safe raw-profile paths,
      merge every shard with `llvm-profdata`, and retain missing/corrupt-profile
      diagnostics.
- [x] Export machine-readable line, branch, function, and region coverage plus
      the exact unique GoogleTest name/label/executable inventory.
- [x] Define and document engine-owned production roots and deterministic
      exclusions for tests, generated files, vcpkg/external code, compiler
      runtime code, and other non-product inputs.
- [x] Add a comparison mode for identical-production-commit test refactors that
      normalizes paths and fails when a previously covered production region or
      branch disappears, independent of the aggregate percentage.
- [x] Emit informational diff coverage for changed production lines without
      making a permanent threshold required until baseline stability is
      reviewed under `CI-009`.
- [x] Publish the report and raw/merged profile diagnostics from a dedicated
      default-branch, scheduled, or manually triggered job; final lifecycle
      placement remains `CI-009`.

## Tests
- [x] Add synthetic tooling regressions with at least two instrumented
      executables and parallel profile writes, proving complete object/profile
      aggregation.
- [x] Prove missing executable mappings, zero selected tests, corrupt profiles,
      production-source drift in parity mode, and a lost covered region fail
      nonzero with actionable diagnostics.
- [x] Prove normalized path/exclusion handling is deterministic across two
      build directories.
- [x] Capture one named baseline from the complete selected CPU cohort and
      validate its JSON/export artifacts.

## Docs
- [x] Add the CPU source-coverage policy to
      `docs/benchmarking/ci-policy.md`, distinguishing test inventory, source
      regions, assertions/invariants, and backend-specific evidence.
- [x] Document local reproduction, included/excluded roots, comparison
      preconditions, artifact schema, and why a percentage alone is not a
      no-coverage-loss proof.
- [x] Update process/task indexes and regenerate `tasks/SESSION-BRIEF.md` on
      retirement.

## Acceptance criteria
- [x] One command sequence produces a complete CPU test inventory, merged
      profile, and line/branch/region export using every registered CPU binary.
- [x] Parallel shards cannot overwrite one another and missing mappings or
      profiles cannot yield a success-shaped partial report.
- [x] On an identical production commit, removing execution of a previously
      covered region causes the test-refactor parity check to fail even if the
      global percentage stays equal or rises.
- [x] The baseline records compiler/preset/backend identity and deterministic
      exclusions so later results are comparable.
- [x] Existing correctness, sanitizer, and GPU gates remain authoritative and
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

Exact hosted evidence:

- `ci-linux-clang` run `29570979186` passed all 4,062 selected CPU entries,
  routing reconciliation, layering, compile-hotspot reporting, and SLO checks.
- Initial coverage run `29570990788` failed closed on a CTest-incompatible
  selector before collection; commit `0d26bbaf` corrected the selector.
- Corrected v1 run `29573162676` established the first complete baseline.
- Final v2 run `29575099005` passed in 27m16s: configure 6.454s, build
  1,511.530s, collection 50.016s, 2,149 Ninja edges, and 4,062 selected tests.
  It represented 524 production files and covered 70,207 unique regions,
  34,339 branch arms, 10,107 functions, and 113,817 lines.

## Forbidden changes
- Reporting engine-wide coverage from only `IntrinsicCoreTests` or another
  representative executable.
- Treating equal aggregate percentages or equal test counts as region parity.
- Hiding an uninstrumented/missing binary with a warning-only path.
- Uploading configured build trees or BMIs as cross-job artifacts.
