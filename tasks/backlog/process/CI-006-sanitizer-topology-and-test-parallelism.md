---
id: CI-006
theme: H
depends_on:
  - CI-003
  - CI-005
---
# CI-006 — Remove duplicate sanitizer work and isolate variants

## Goal
- Define one explicit, isolated sanitizer topology so CI does not pay for
  overlapping combined/dedicated variants or accidentally run performance
  gates in a reused sanitizer build tree.

## Non-goals
- No reduction in ASan or UBSan test coverage.
- No object, build-tree, or BMI sharing across sanitizer variants.
- No final CTest/process/Scheduler concurrency policy; `CI-008` owns measured
  worker budgets after the retained variants are known.

## Context
- Owner: CMake presets and `.github/workflows/ci-sanitizers.yml` plus the full
  CPU workflow.
- Hidden preset `base` enables `INTRINSIC_ENABLE_SANITIZERS=ON`; `ci` inherits
  the combined project sanitizer configuration. Dedicated ASan and UBSan matrix
  jobs disable that option and inject separate compiler/linker flags, rebuilding
  both variants from scratch.
- Representative `CI-003` data: UBSan configured in 7.306s, built in ~14m40s,
  tested 3,592 cases in 110.62s, and completed in 17m16s. The combined `ci`
  configuration differs, so the measurements cannot justify keeping both.
- `nightly-deep.yml` repeatedly reconfigures the same `build/ci` directory from
  the project-default combined sanitizer setup to ASan and then UBSan, before
  running benchmark/SLO work from that last configured tree. Variant identity
  and the intended unsanitized performance semantics are therefore ambiguous.
- `ci-fast` from `CI-005` is explicitly unsanitized. This task decides whether
  full `ci-linux-clang` should also be unsanitized after proving the dedicated
  jobs select equivalent CPU coverage.

## Required changes
- [ ] Add explicit, named presets for unsanitized full CPU, ASan, and UBSan
      configurations with distinct binary and vcpkg install directories; stop
      overriding one `build/ci` tree with ad-hoc sanitizer flags in every
      workflow, including `nightly-deep`.
- [ ] Inventory test selection across the current combined gate and dedicated
      ASan/UBSan gates; make any intended exclusions explicit and machine-
      checked.
- [ ] If dedicated ASan+UBSan jobs provide the intended required coverage,
      make the general full CPU preset unsanitized and remove the redundant
      combined sanitizer compile; otherwise retain the combined gate and
      document the unique defect class/coverage it owns.
- [ ] Keep every sanitizer in an isolated build/install directory and telemetry
      identity.
- [ ] Run benchmark/SLO/performance work only from an explicit unsanitized
      non-sanitizer tree, never whichever sanitizer last reconfigured a shared
      tree. `CI-009` owns the final optimized Release preset and lifecycle.
- [ ] Make selectors, selected-case inventories, preset identity, sanitizer
      flags, and timing metadata mechanically agree for every retained variant.

## Tests
- [ ] Add preset/workflow regression coverage proving sanitizer identity,
      flags, build directory, and test selectors cannot alias.
- [ ] Add a test-selection parity report for unsanitized, ASan, and UBSan jobs.
- [ ] Run the full selected suite in each retained sanitizer configuration.
- [ ] Prove nightly benchmark/SLO steps consume a named non-sanitizer tree and
      cannot inherit ASan/UBSan flags from a preceding step.
- [ ] Compare build/test/total median and p95 against `CI-003` using identical
      selectors; leave concurrency A/B work to `CI-008`.

## Docs
- [ ] Document the required sanitizer matrix, exclusions, isolated directories,
      performance-build identity, and local reproduction commands in the
      `docs/benchmarking/ci-policy.md` CI/build policy.
- [ ] Update `AGENTS.md` if the canonical `ci` preset's sanitizer semantics
      change.
- [ ] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [ ] Every required sanitizer variant has one explicit preset, isolated build
      tree, and non-overlapping reason to exist.
- [ ] ASan and UBSan coverage is preserved and mechanically compared to the
      intended CPU selector.
- [ ] Benchmark/SLO work is isolated from sanitizer trees; `CI-009`, not this
      task, owns its final optimized Release configuration and lifecycle.
- [ ] Any removal of combined sanitizers is justified by dedicated-gate parity,
      not by a weakened selector.

## Verification
```bash
cmake --preset ci-asan
cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-ubsan
cmake --build --preset ci-ubsan --target IntrinsicCpuTests
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tests/regression/tooling/Test.SanitizerPresets.py
python3 tools/repo/check_pr_contract.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Dropping ASan or UBSan from required CI to improve wall time.
- Reusing objects or BMIs across unsanitized/ASan/UBSan configurations.
- Running benchmark/SLO work from a tree last configured for a sanitizer.
- Hiding sanitizer findings through broad exclusions or quarantine.
