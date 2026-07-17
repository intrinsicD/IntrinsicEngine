# tools/ci

CI helper scripts and workflow validation tools.

## Current scripts

- `check_workflow_names.py`: validates workflow file allowlist, `name` consistency, explicit `on` triggers, and readability (no one-line compressed YAML). Runs in `ci-docs.yml`.
- `check_prerequisites.py`: fails fast when CI steps are blocked by missing build artifacts (test binaries, inventories) instead of surfacing a downstream error. Invoked by `pr-fast.yml`, `ci-linux-clang.yml`, `ci-vulkan.yml`, and `nightly-deep.yml`.
- `time_command.py`: runs a command, streams its output, and writes an elapsed wall-clock phase report for gate-timing aggregation. Invoked by `pr-fast.yml`, `ci-linux-clang.yml`, `ci-vulkan.yml`, `ci-bench-smoke.yml`, `ci-sanitizers.yml`, and `nightly-deep.yml`.
- `aggregate_gate_timing.py`: aggregates the per-phase configure/build/test reports emitted by `time_command.py` into one machine-readable CI gate result and records the complete configured backend/platform identity from `CMakeCache.txt`. Invoked by `pr-fast.yml`, `ci-linux-clang.yml`, `ci-vulkan.yml`, `ci-bench-smoke.yml`, and `ci-sanitizers.yml`.
- `validate_gate_timing_baseline.py`: validates the CI-003 historical gate-latency baseline and statistics payloads. Exercised by `tests/regression/tooling/Test.CiTiming.py`; see `benchmarks/ci/README.md`.
- `ccache_ci.py`: validates the retained CI ccache policy (configured launcher/mode/digest identity) and exports ccache statistics. Part of the CI-007 `pr-fast.yml` policy.
- `ccache_module_invalidation_probe.py`: exercises ccache reuse across a hermetic C++23 module-interface change to prove exported-interface edits invalidate importers. Part of the retained CI-007 `pr-fast.yml` policy.
- `touched_scope.py`: plans (or runs) conservative build/test/structural
  verification commands for the touched repository scope. Kernel-checker,
  policy, regression, and `Runtime.Engine.cppm` changes select the live
  convergence guard plus its synthetic regression. Local iteration aid; not
  yet a workflow gate (CI-005 tracks promoting it).
- `run_repo_hygiene_checks.sh`: warning-mode wrapper running the canonical
  `check_root_hygiene.py` policy check once, followed by `check_doc_links.py`.
  Local convenience; not wired into a workflow.
- `run_clean_workshop_review.sh`: clean-workshop architecture review bundle (WORKSHOP-009) running the layering, allowlist-quality, task-policy, and doc-link validators. Local convenience; not wired into a workflow.

## Notes

- `check_workflow_names.py --strict` reserves enforcement for the full canonical workflow set, including `nightly-deep.yml`.
