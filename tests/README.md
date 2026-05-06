# Tests

Tests are organized by taxonomy-owned roots:

- `unit/` — isolated subsystem behavior.
- `contract/` — API, layering, lifecycle, and interoperability contracts.
- `integration/` — controlled cross-subsystem behavior.
- `regression/` — fixed reproducers for previously observed failures.
- `gpu/` — opt-in GPU/Vulkan coverage.
- `benchmark/` — benchmark smoke/SLO checks.
- `support/` — shared test-only fixtures and helpers.

## CTest labels

CTest labels are assigned per executable in `tests/CMakeLists.txt` and should
describe both the test category and the subsystem or capability being exercised.
The documented label set is:

- categories: `unit`, `contract`, `integration`, `regression`, `benchmark`,
  `slo`.
- subsystems/ownership: `assets`, `build`, `core`, `ecs`, `geometry`,
  `graphics`, `headless`, `platform`, `runtime`.
- capabilities/backends: `glfw`, `gpu`, `vulkan`.
- opt-in filters: `slow`, `flaky-quarantine`.

Use `slow` for valid tests that should not run in fast PR or default local CPU
correctness gates, including executables that boot the full headless engine,
initialize Vulkan in a non-`gpu`-only path, run benchmark/SLO thresholds, or
regularly exceed one second of wall-clock time on the reference Linux-clang
runner. Do not add `slow` to pure CPU unit or contract suites merely because
they contain many cheap cases.

Use `flaky-quarantine` only as a temporary quarantine. Any test or executable
with this label must have a linked task ID, a reason, and a removal condition in
the relevant source or task record.

Common gates:

```bash
ctest --test-dir build/ci --output-on-failure -L 'unit|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 -j$(nproc)
```

## New test naming

New C++ test source files should use the `Test.<Name>.cpp` format.
Do not introduce new `Test_<Name>.cpp` files. Existing older `Test_*.cpp` files
are compatibility carryover and should only be renamed as part of an explicit
mechanical cleanup task.
