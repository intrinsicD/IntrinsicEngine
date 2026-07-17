---
id: CI-006
theme: H
depends_on:
  - CI-003
  - CI-005
---
# CI-006 — Remove duplicate sanitizer work and isolate variants

## Status
- Completed on 2026-07-17; implementation commits: `a7ae8e7f` and
  `14ff5259`; activation commit: `b19107bc`; maturity: `Operational`.
- Five passing hosted samples were retained for each isolated CPU identity. The
  failed ASan artifact-finalization attempt tracked by `BUG-111` was excluded;
  its bounded same-SHA specific-job rerun passed and supplied the replacement
  sample.

## Evidence
- Fifteen hosted selection reports, five for each of `none`, `asan`, and
  `ubsan`, each resolved 26 producers and 4,062 cases to digest
  `07c9f615629327c0502cd4aa73c411de41693b88ccc0ea80dbabb624cb6cf08b`.
  The real three-variant comparison passed with schema
  `intrinsic.cpu-test-selection-parity/v1`.
- ASan and UBSan samples used runs `29589810741`, `29589810850`,
  `29589810886`, `29589810898`, and `29589811057` at `a7ae8e7f`; the ASan
  sample for `29589810886` is successful attempt 2, job `87928232616`.
  Unsanitized samples used runs `29592744115`, `29592761257`, `29592761260`,
  `29592761292`, and `29592761299` at `44da41e3`.
- Median/p95 seconds were: unsanitized configure `34.722/37.876`, build
  `1707.296/1746.903`, test `32.292/33.472`, total `1771.333/1816.077`;
  ASan configure `10.855/14.579`, build `1698.595/1748.666`, test
  `523.328/526.892`, total `2232.778/2288.766`; UBSan configure
  `10.546/13.459`, build `1539.424/1730.877`, test `178.098/183.850`, total
  `1733.820/1923.281`.
- The immutable `CI-003` populations used different selectors and ad-hoc or
  combined sanitizer identities. They remain historical policy evidence but
  are formally non-comparable to this first isolated identical-selector
  baseline; no causal speedup or concurrency claim is made. `CI-008` owns
  concurrency A/B evidence.

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
- At activation, hidden preset `base` enabled
  `INTRINSIC_ENABLE_SANITIZERS=ON`; `ci` inherited the combined project
  sanitizer configuration. Dedicated ASan and UBSan matrix jobs disabled that
  option and injected separate compiler/linker flags, rebuilding both variants
  from scratch.
- Representative `CI-003` data: UBSan configured in 7.306s, built in ~14m40s,
  tested 3,592 cases in 110.62s, and completed in 17m16s. The combined `ci`
  configuration differs, so the measurements cannot justify keeping both.
- At activation, `nightly-deep.yml` repeatedly reconfigured the same
  `build/ci` directory from the project-default combined sanitizer setup to
  ASan and then UBSan before running benchmark/SLO work from that last
  configured tree. Variant identity and the intended unsanitized performance
  semantics were therefore ambiguous.
- `ci-fast` from `CI-005` was already explicitly unsanitized. This task
  determined that full `ci-linux-clang` could also be unsanitized after proving
  the dedicated jobs select equivalent CPU coverage.

## Required changes
- [x] Add explicit, named presets for unsanitized full CPU, ASan, and UBSan
      configurations with distinct binary and vcpkg install directories; stop
      overriding one `build/ci` tree with ad-hoc sanitizer flags in every
      workflow, including `nightly-deep`.
- [x] Inventory test selection across the current combined gate and dedicated
      ASan/UBSan gates; make any intended exclusions explicit and machine-
      checked.
- [x] If dedicated ASan+UBSan jobs provide the intended required coverage,
      make the general full CPU preset unsanitized and remove the redundant
      combined sanitizer compile; otherwise retain the combined gate and
      document the unique defect class/coverage it owns.
- [x] Keep every sanitizer in an isolated build/install directory and telemetry
      identity.
- [x] Run benchmark/SLO/performance work only from an explicit unsanitized
      non-sanitizer tree, never whichever sanitizer last reconfigured a shared
      tree. `CI-009` owns the final optimized Release preset and lifecycle.
- [x] Make selectors, selected-case inventories, preset identity, sanitizer
      flags, and timing metadata mechanically agree for every retained variant.

## Tests
- [x] Add preset/workflow regression coverage proving sanitizer identity,
      flags, build directory, and test selectors cannot alias.
- [x] Add a test-selection parity report for unsanitized, ASan, and UBSan jobs.
- [x] Run the full selected suite in each retained sanitizer configuration.
- [x] Prove nightly benchmark/SLO steps consume a named non-sanitizer tree and
      cannot inherit ASan/UBSan flags from a preceding step.
- [x] Compare the new isolated variants with identical selectors, retain
      `CI-003` as formally non-comparable historical context, and leave
      concurrency A/B work to `CI-008`.

## Docs
- [x] Document the required sanitizer matrix, exclusions, isolated directories,
      performance-build identity, and local reproduction commands in the
      `docs/benchmarking/ci-policy.md` CI/build policy.
- [x] Update `AGENTS.md` if the canonical `ci` preset's sanitizer semantics
      change.
- [x] Regenerate `tasks/SESSION-BRIEF.md` on retirement.

## Acceptance criteria
- [x] Every required sanitizer variant has one explicit preset, isolated build
      tree, and non-overlapping reason to exist.
- [x] ASan and UBSan coverage is preserved and mechanically compared to the
      intended CPU selector.
- [x] Benchmark/SLO work is isolated from sanitizer trees; `CI-009`, not this
      task, owns its final optimized Release configuration and lifecycle.
- [x] Any removal of combined sanitizers is justified by dedicated-gate parity,
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
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Dropping ASan or UBSan from required CI to improve wall time.
- Reusing objects or BMIs across unsanitized/ASan/UBSan configurations.
- Running benchmark/SLO work from a tree last configured for a sanitizer.
- Hiding sanitizer findings through broad exclusions or quarantine.
