# tools/ci

CI helper scripts and workflow validation tools.

## Current scripts

- `check_workflow_names.py`: validates workflow file allowlist, `name` consistency, explicit `on` triggers, and readability (no one-line compressed YAML). Runs in `ci-docs.yml`.
- `check_prerequisites.py`: fails fast when CI steps are blocked by missing build artifacts (test binaries, inventories) instead of surfacing a downstream error. Invoked by `ci-linux-clang.yml`, `ci-vulkan.yml`, and `nightly-deep.yml`.
- `time_command.py`: runs a command, streams its output, and writes an elapsed wall-clock phase report for gate-timing aggregation. Invoked by `pr-fast.yml`, `ci-linux-clang.yml`, `ci-vulkan.yml`, `ci-bench-smoke.yml`, `ci-sanitizers.yml`, `ci-source-coverage.yml`, and `nightly-deep.yml`.
- `aggregate_gate_timing.py`: aggregates the per-phase configure/build/test reports emitted by `time_command.py` into one machine-readable CI gate result and records the complete configured backend/platform identity from `CMakeCache.txt`. Invoked by `pr-fast.yml`, `ci-linux-clang.yml`, `ci-vulkan.yml`, `ci-bench-smoke.yml`, `ci-sanitizers.yml`, and `ci-source-coverage.yml`.
- `source_coverage.py`: shared fail-closed Clang source-coverage collection,
  normalization, identity, and parity primitives.
- `run_source_coverage.py`: reconciles the canonical `IntrinsicCpuTests`
  inventory, executes each selected GoogleTest producer once with its exact
  enabled-case filter, reconciles exact execution from per-target XML, merges
  collision-safe execution profiles, and writes the raw and normalized CPU
  coverage artifacts. Discovery profiles are isolated and retained for
  diagnostics but excluded from the merge.
- `compare_source_coverage.py`: validates two normalized coverage reports and,
  in `--test-only-refactor` mode, rejects identity drift or loss of any
  previously covered production region or branch outcome.
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
  cmake --build --preset ci-coverage-cpu --target IntrinsicCpuTests
  python3 tools/ci/run_source_coverage.py \
    --build-dir build/ci-coverage-cpu \
    --output build/ci-coverage-cpu/coverage \
    --preset ci-coverage-cpu \
    --diff-base HEAD^
  ```

  The output directory must be absent or empty. The collector retains raw
  profiles and diagnostics rather than mixing them with a prior run.

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
