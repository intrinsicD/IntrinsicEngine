---
id: CI-006
theme: H
depends_on:
  - CI-003
  - CI-005
---
# CI-006 — Remove duplicate sanitizer work and calibrate test parallelism

## Goal
- Define one explicit sanitizer topology and measured CTest concurrency per
  sanitizer so CI does not pay for overlapping combined/dedicated variants or
  oversubscribe hosted runners.

## Non-goals
- No reduction in ASan or UBSan test coverage.
- No object, build-tree, or BMI sharing across sanitizer variants.
- No parallelism increase based only on `nproc`.

## Context
- Owner: CMake presets and `.github/workflows/ci-sanitizers.yml` plus the full
  CPU workflow.
- Hidden preset `base` enables `INTRINSIC_ENABLE_SANITIZERS=ON`; `ci` inherits
  the combined project sanitizer configuration. Dedicated ASan and UBSan matrix
  jobs disable that option and inject separate compiler/linker flags, rebuilding
  both variants from scratch.
- Representative `CI-003` data: UBSan configured in 7.306s, built in ~14m40s,
  tested 3,592 cases serially in 110.62s, and completed in 17m16s. The combined
  `ci` gate tested in ~217.53s with `-j$(nproc)`, but the configurations differ,
  so this is a hypothesis about oversubscription, not evidence that serial is
  universally faster.
- Runtime-bearing tests can initialize `Core::Tasks::Scheduler` with
  `hardware_concurrency - 1` workers when configured with zero. Running many
  such processes under CTest `-j$(nproc)` can multiply runnable workers beyond
  the runner allocation.
- `ci-fast` from `CI-005` is explicitly unsanitized. This task decides whether
  full `ci-linux-clang` should also be unsanitized after proving the dedicated
  jobs select equivalent CPU coverage.

## Required changes
- [ ] Add explicit, named presets for unsanitized full CPU, ASan, and UBSan
      configurations; stop overriding one `build/ci` preset/tree with ad-hoc
      sanitizer flags.
- [ ] Inventory test selection across the current combined gate and dedicated
      ASan/UBSan gates; make any intended exclusions explicit and machine-
      checked.
- [ ] Measure CTest `-j1`, `-j2`, and `-j4` for ASan and UBSan on comparable
      hosted runners, recording wall time, failures, timeouts, peak resource
      diagnostics available from the runner, and selected test count.
- [ ] Choose and document sanitizer-specific concurrency from median/p95 data.
- [ ] If dedicated ASan+UBSan jobs provide the intended required coverage,
      make the general full CPU preset unsanitized and remove the redundant
      combined sanitizer compile; otherwise retain the combined gate and
      document the unique defect class/coverage it owns.
- [ ] Keep every sanitizer in an isolated build/install directory and telemetry
      identity.

## Tests
- [ ] Add preset/workflow regression coverage proving sanitizer identity,
      flags, build directory, and test selectors cannot alias.
- [ ] Add a test-selection parity report for unsanitized, ASan, and UBSan jobs.
- [ ] Run the full selected suite in each retained sanitizer configuration.
- [ ] Compare build/test/total median and p95 against `CI-003`; do not compare
      serial UBSan directly with a different combined-sanitizer configuration.

## Docs
- [ ] Document the required sanitizer matrix, exclusions, concurrency evidence,
      and local reproduction commands in the CI/build documentation.
- [ ] Update `AGENTS.md` if the canonical `ci` preset's sanitizer semantics
      change.
- [ ] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [ ] Every required sanitizer variant has one explicit preset, isolated build
      tree, and non-overlapping reason to exist.
- [ ] ASan and UBSan coverage is preserved and mechanically compared to the
      intended CPU selector.
- [ ] CTest concurrency is backed by comparable A/B measurements rather than
      host core count alone.
- [ ] Any removal of combined sanitizers is justified by dedicated-gate parity,
      not by a weakened selector.

## Verification
```bash
cmake --preset ci-asan
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j<measured-value>
cmake --preset ci-ubsan
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j<measured-value>
python3 tests/regression/tooling/Test.SanitizerPresets.py
python3 tools/repo/check_pr_contract.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Dropping ASan or UBSan from required CI to improve wall time.
- Reusing objects or BMIs across unsanitized/ASan/UBSan configurations.
- Selecting concurrency from incomparable configurations or one run.
- Hiding sanitizer findings through broad exclusions or quarantine.
