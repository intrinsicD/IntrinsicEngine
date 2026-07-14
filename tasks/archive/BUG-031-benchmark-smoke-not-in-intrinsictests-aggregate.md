---
id: BUG-031
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-031 — Documented agent gate cannot pass a fresh tree: benchmark smoke binary missing from the `IntrinsicTests` aggregate

## Goal

- The documented agent verification flow — `cmake --build --preset ci --target IntrinsicTests` followed by the default CPU gate — passes on a fresh tree: `IntrinsicBenchmarkSmoke.Run`/`IntrinsicBenchmarkSmoke.Validate` find their executable instead of failing as "Not Run".

## Non-goals

- No change to what the benchmark smoke measures or its result schema.
- No re-labeling that silently drops the benchmark smoke out of the default gate (the in-tree comment states it belongs there deliberately).
- No CI workflow restructuring beyond what the one-line dependency fix requires.

## Context

- Symptom: in this container (2026-06-11), the documented flow from AGENTS.md §5/§7 (`cmake --preset ci`; `cmake --build --preset ci --target IntrinsicTests`; `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`) yields two guaranteed failures: tests #1/#2 `IntrinsicBenchmarkSmoke.Run`/`.Validate` report **Not Run** — "Could not find executable …/build/ci/bin/IntrinsicBenchmarkSmoke".
- Mechanism (verified): `benchmarks/CMakeLists.txt` defines `add_executable(IntrinsicBenchmarkSmoke …)` (line 8) and registers it with CTest under labels `benchmark;geometry;physics` with the explicit comment "Register the smoke runner as a CTest case **so the default CPU gate exercises** the benchmark workloads" (lines ~51-66). The default gate's exclusion `-LE 'gpu|vulkan|slow|flaky-quarantine'` does not exclude `benchmark` — intent and labels agree the test runs in the default gate. But the executable never joins the `IntrinsicTests` aggregate: tests/CMakeLists.txt:1198-1201 wires only targets registered in the `INTRINSIC_REGISTERED_TEST_TARGETS` global property (via `intrinsic_test_executable`), and `benchmarks/` neither uses the helper nor calls `add_dependencies(IntrinsicTests IntrinsicBenchmarkSmoke)`.
- Why CI doesn't catch it: `ci-linux-clang.yml` builds the **full preset** (`cmake --build --preset ci`, step "Build full CPU targets"), which builds every target including the smoke runner — so the lane never exercises the documented `IntrinsicTests`-only flow that agents and Codex verification use (AGENTS.md §10 explicitly names `IntrinsicTests` as the meaningful build target for agent verification). The gap bites exactly the audience the documented flow serves.
- Impact: every agent session following the contract sees a baseline-red gate (2 failures), which normalizes failure noise and masks real regressions; "preserve or improve pass rate" (§7) becomes unmeasurable from a fresh tree.
- Owner/layer: build wiring (`benchmarks/CMakeLists.txt`, possibly `tests/CMakeLists.txt` helper reuse). No engine code.

## Required changes

- [x] Make `IntrinsicBenchmarkSmoke` part of the `IntrinsicTests` aggregate: in `benchmarks/CMakeLists.txt`, guard-add `if(TARGET IntrinsicTests) add_dependencies(IntrinsicTests IntrinsicBenchmarkSmoke) endif()` next to the CTest registration (or register through the same global-property helper the test tree uses — pick whichever keeps a single registration idiom and note the choice in the PR).
- [x] Confirm ordering: `benchmarks/` must be added after the aggregate target exists (check `add_subdirectory` order in the root `CMakeLists.txt`; if `benchmarks/` precedes `tests/`, use the global-property route instead of `if(TARGET …)`).

## Tests

- [x] Fresh-tree proof: clean configure, build **only** `IntrinsicTests`, run the default gate — `IntrinsicBenchmarkSmoke.Run` and `.Validate` execute and pass (no "Not Run").
- [x] Full-preset build still green (CI parity).

## Docs

- [x] None expected; AGENTS.md §5/§7/§10 already describe the intended flow — this change makes the tree match the docs. Update `benchmarks/README.md` only if it documents the target relationships.

## Acceptance criteria

- [x] `cmake --build --preset ci --target IntrinsicTests` produces `build/ci/bin/IntrinsicBenchmarkSmoke`.
- [x] Default CPU gate on a fresh tree has zero "Not Run" entries from the benchmark smoke pair.
- [x] No duplicate/conflicting registration of the smoke runner.

## Verification

```bash
rm -rf build/ci
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'IntrinsicBenchmarkSmoke' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

2026-06-12 results:
- Commit: pending local BUG loop closure commit.
- The current tree already registers `IntrinsicBenchmarkSmoke` through the shared `INTRINSIC_REGISTERED_TEST_TARGETS` aggregate path in `benchmarks/CMakeLists.txt`.
- `cmake --build --preset ci --target IntrinsicTests` produced `build/ci/bin/IntrinsicBenchmarkSmoke`.
- Focused BUG regression set passed `IntrinsicBenchmarkSmoke.Run` and `IntrinsicBenchmarkSmoke.Validate` (2/2).
- Default CPU-supported CTest gate passed with no benchmark "Not Run" entries.

## Forbidden changes

- Excluding the `benchmark` label from the documented default gate as the "fix" (contradicts the in-tree intent comment; that decision would need its own task and AGENTS.md §7 sync).
- Weakening `validate_benchmark_results.py --strict` or the smoke's result-schema checks.

## Maturity

- Target: `CPUContracted` — build/test wiring fully proven by the default gate on a fresh tree. No `Operational` follow-up is owed.
