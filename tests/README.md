# Tests

Tests are organized by taxonomy-owned roots:

- `unit/` — isolated subsystem behavior.
- `contract/` — API, layering, lifecycle, and interoperability contracts.
- `integration/` — controlled cross-subsystem behavior.
- `regression/` — fixed reproducers for previously observed failures.
- `gpu/` — opt-in GPU/Vulkan coverage.
- `benchmark/` — benchmark smoke/SLO checks.
- `support/` — shared test-only fixtures and helpers.

## New test naming

New C++ test source files should use the `Test.<Name>.cpp` format.
Do not introduce new `Test_<Name>.cpp` files. Existing older `Test_*.cpp` files
are compatibility carryover and should only be renamed as part of an explicit
mechanical cleanup task.

