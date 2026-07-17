# tools/ci

CI helper scripts and workflow validation tools.

## Current scripts

- `check_workflow_names.py`: validates workflow file allowlist, `name` consistency, explicit `on` triggers, and readability (no one-line compressed YAML). Runs in `ci-docs.yml`.
- `check_prerequisites.py`: fails fast when CI steps are blocked by missing build artifacts (test binaries, inventories) instead of surfacing a downstream error. Invoked by `ci-linux-clang.yml`, `ci-vulkan.yml`, and `nightly-deep.yml`.
- `time_command.py`: runs a command, streams its output, and writes an elapsed wall-clock phase report for gate-timing aggregation. Invoked by `pr-fast.yml`, `ci-linux-clang.yml`, `ci-vulkan.yml`, `ci-bench-smoke.yml`, `ci-sanitizers.yml`, `ci-source-coverage.yml`, and `nightly-deep.yml`.
- `aggregate_gate_timing.py`: aggregates the per-phase configure/build/test reports emitted by `time_command.py` into one machine-readable CI gate result and records the complete configured backend/platform identity from `CMakeCache.txt`. Invoked by `pr-fast.yml`, `ci-linux-clang.yml`, `ci-vulkan.yml`, `ci-bench-smoke.yml`, `ci-sanitizers.yml`, and `ci-source-coverage.yml`.
- `source_coverage.py`: shared fail-closed Clang source-coverage collection,
  normalization, identity, and parity primitives.
- `run_source_coverage.py`: reconciles either the default `IntrinsicCpuTests`
  inventory or the `cpu-coverage` cohort that adds ordinary slow correctness,
  executes each selected GoogleTest producer once with its exact enabled-case
  filter, reconciles exact execution from per-target XML, merges collision-safe
  execution profiles, and writes the raw and normalized CPU coverage artifacts.
  Discovery profiles are isolated and retained for diagnostics but excluded
  from the merge.
- `compare_source_coverage.py`: validates two normalized coverage reports and,
  in `--test-only-refactor` mode, rejects identity drift or loss of any
  previously covered production region or branch outcome. Its
  `--test-cohort-transition` mode additionally binds each report to its sibling
  test inventory, resolves canonical manifest CTest names through the retained
  `ctest_name` to `gtest_filter` mapping, permits only the manifest-declared
  fast sentinels, requires every moved heavy case in both populations with only
  the `slow` label added, and compares common-case working-directory plus
  production coverage parity. Claim-grade workflow collection runs one test
  producer at a time so unrelated process scheduling cannot create artificial
  branch/region drift between otherwise identical product builds.
- `test_cohort_manifest.py`: parses the strict shared fast-to-slow transition
  manifest consumed by timing and source-coverage parity.
- `validate_gate_timing_baseline.py`: validates the CI-003 historical gate-latency baseline and statistics payloads. Exercised by `tests/regression/tooling/Test.CiTiming.py`; see `benchmarks/ci/README.md`.
- `ccache_ci.py`: validates the retained CI ccache policy (configured launcher/mode/digest identity) and exports ccache statistics. Part of the CI-007 `pr-fast.yml` policy.
- `ccache_module_invalidation_probe.py`: exercises ccache reuse across a hermetic C++23 module-interface change to prove exported-interface edits invalidate importers. Part of the retained CI-007 `pr-fast.yml` policy.
- `touched_scope.py`: plans (or runs) conservative build/test/structural
  verification for the exact name-status diff from the unique merge base of
  the supplied base/head refs to the head. It drives `pr-fast` through
  pre-configure classification, structural-only execution, strict
  post-configure test-registry reconciliation, and focused or bounded broad
  build/test actions. Its route directory records changed files, reasons,
  labels, producer/case inventories, command closure, fallback state, and
  per-batch timing. Missing or ambiguous input fails closed rather than
  producing an empty plan.
- `cpu_test_selection.py`: captures the exact path-free `IntrinsicCpuTests`
  producer and CTest-case inventory selected by the canonical
  `-LE 'gpu|vulkan|slow|flaky-quarantine'` predicate, validates the configured
  resolved sanitizer identity, and compares reports without treating isolated
  build-directory paths as selection drift. Pull-request and manual
  `ci-linux-clang` runs require the comparison over artifacts from the
  unsanitized job and its reusable ASan/UBSan jobs.
- `collect_test_timing.py`: runs repeated canonical `pr-fast`, `cpu`, or
  `cpu-slow` CTest cohorts at an explicit recorded parallelism, reconciles every
  JUnit result against serial PRE_TEST discovery and aggregate inventories,
  restores the incoming CTest scheduler cost data between samples, and retains
  per-case integer-microsecond timing/status plus host/load diagnostics.
- `test_cohort_parity.py`: compares protocol-complete baseline CPU and PR-fast
  reports with candidate CPU, PR-fast, and ordinary-slow reports plus the
  scheduled slow JUnit. It fails closed on undeclared removals/additions,
  duplicate or overlapping cases, label drift beyond adding `slow`, mismatched
  hosted-run identities, non-passing samples, or a moved case absent, skipped,
  failed, or duplicated in scheduled coverage. The declared transition lives
  in `slow_test_cohort.json`.
- `run_repo_hygiene_checks.sh`: warning-mode wrapper running the canonical
  `check_root_hygiene.py` policy check once, followed by `check_doc_links.py`.
  Local convenience; not wired into a workflow.
- `run_clean_workshop_review.sh`: clean-workshop architecture review bundle (WORKSHOP-009) running the layering, allowlist-quality, task-policy, and doc-link validators. Local convenience; not wired into a workflow.

## Notes

- `check_workflow_names.py --strict` reserves enforcement for the full
  canonical workflow set, including `ci-source-coverage.yml` and
  `nightly-deep.yml`.
- The reproducible local CPU source-coverage sequence is:

  ```bash
  cmake --preset ci-coverage-cpu --fresh
  cmake --build --preset ci-coverage-cpu --target IntrinsicCpuCoverageTests
  python3 tools/ci/run_source_coverage.py \
    --build-dir build/ci-coverage-cpu \
    --output build/ci-coverage-cpu/coverage \
    --preset ci-coverage-cpu \
    --cohort cpu-coverage \
    --diff-base HEAD^
  ```

  The output directory must be absent or empty. The collector retains raw
  profiles and diagnostics rather than mixing them with a prior run.

- Compare a declared fast-to-slow split on identical production/build inputs
  with:

  ```bash
  python3 tools/ci/compare_source_coverage.py \
    --baseline /path/to/baseline/coverage.json \
    --candidate /path/to/candidate/coverage.json \
    --test-cohort-transition tools/ci/slow_test_cohort.json
  ```

  Each `coverage.json` must remain beside its bound `test-inventory.json`.
  Both reports must use `cpu-coverage`, whose
  `IntrinsicCpuCoverageTests` aggregate excludes benchmark, SLO, GPU/Vulkan,
  and quarantined ownership but includes ordinary `slow` correctness.

- Collect comparable five-sample cohort reports after building their exact
  aggregates with:

  ```bash
  python3 tools/ci/collect_test_timing.py \
    --build-dir build/ci-fast --cohort pr-fast \
    --samples 5 --parallel $(nproc) \
    --output build/ci-fast/test-timing-pr-fast
  python3 tools/ci/collect_test_timing.py \
    --build-dir build/ci --cohort cpu \
    --samples 5 --parallel $(nproc) \
    --output build/ci/test-timing-cpu
  python3 tools/ci/collect_test_timing.py \
    --build-dir build/ci --cohort cpu-slow \
    --samples 5 --parallel $(nproc) \
    --output build/ci/test-timing-cpu-slow
  ```

  Each output directory must be absent or empty. The collector captures its
  canonical aggregate and selector identity, performs serial PRE_TEST
  discovery, resets and restores CTest scheduler cost data around every
  measured sample, and writes `report.json`, per-sample JUnit/log files,
  per-case duration/status data, a selection digest, and host/load diagnostics.
  It retains those artifacts and exits nonzero if selection drifts or any
  measured CTest sample fails.

- Verify the declared fast-to-slow transition and exact scheduled execution
  with:

  ```bash
  python3 tools/ci/test_cohort_parity.py \
    --manifest tools/ci/slow_test_cohort.json \
    --baseline-cpu /path/to/baseline/cpu/report.json \
    --baseline-pr-fast /path/to/baseline/pr-fast/report.json \
    --candidate-cpu /path/to/candidate/cpu/report.json \
    --candidate-pr-fast /path/to/candidate/pr-fast/report.json \
    --candidate-slow /path/to/candidate/cpu-slow/report.json \
    --scheduled-slow-junit /path/to/candidate/cpu-slow.junit.xml
  ```

  The baseline CPU and PR-fast inputs must share one SHA; the three candidate
  reports must share another SHA; and all reports must share the same hosted
  runner protocol. The comparator requires at least five successful samples,
  exact declared fast removals and sentinel additions in both CPU and PR-fast,
  an ordinary-slow population equal to the declared moved cases, no fast/slow
  overlap, and passing moved cases in the scheduled JUnit.

- Capture and compare unsanitized, ASan, and UBSan CPU selections with:

  ```bash
  python3 tools/ci/cpu_test_selection.py capture \
    --build-dir build/ci --preset ci --expected-sanitizer none \
    --output build/ci/cpu-test-selection.json
  python3 tools/ci/cpu_test_selection.py capture \
    --build-dir build/ci-asan --preset ci-asan --expected-sanitizer asan \
    --output build/ci-asan/cpu-test-selection.json
  python3 tools/ci/cpu_test_selection.py capture \
    --build-dir build/ci-ubsan --preset ci-ubsan --expected-sanitizer ubsan \
    --output build/ci-ubsan/cpu-test-selection.json
  python3 tools/ci/cpu_test_selection.py compare \
    --report build/ci/cpu-test-selection.json \
    --report build/ci-asan/cpu-test-selection.json \
    --report build/ci-ubsan/cpu-test-selection.json \
    --require-sanitizer none --require-sanitizer asan \
    --require-sanitizer ubsan \
    --output build/cpu-test-selection-parity.json
  ```
