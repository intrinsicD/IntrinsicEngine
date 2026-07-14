# Layering checker fixtures

Fixtures exercised by `tools/repo/check_layering.py` to assert that the layer
contract from [`/AGENTS.md`](../../../../AGENTS.md) covers both C++23 module
imports (`import Extrinsic.<Layer>.*;`) and CMake `target_link_libraries(...)`
edges. Run via `tests/regression/tooling/Test.CheckLayering.py` and the
fixture-only command in
[`tasks/archive/WORKSHOP-001-layer-check-module-and-cmake-aware.md`](../../../../tasks/archive/WORKSHOP-001-layer-check-module-and-cmake-aware.md).

## Layout

- `positive_*/src/<layer>/...` — clean cases. The checker scans these and
  reports zero violations.
- `negative_*/src/<layer>/...` — single-edge violations. The regression test
  invokes the checker per case and asserts the expected violation message and
  non-zero exit. The top-level scan excludes these directories so the fixture
  root remains clean.

Fixture sources contain only enough syntax to satisfy the checker's regular
expressions (`#include`, `import ...;`, `target_link_libraries(...)`). They
are never compiled.

## Adding a case

Drop a new positive or negative directory beside the existing ones using the
same `<positive|negative>_<short_label>/src/<layer>/` shape, add a single
file demonstrating the edge, and extend `Test.CheckLayering.py` if the case
exercises a new violation type.

`positive_clean_physics` and `negative_physics_imports_higher_layers` pin the
`ARCH-001` physics-layer contract: future `src/physics` modules may import
`core` and `geometry`, but not `ecs`, `runtime`, `graphics`, `platform`, or
`app`.
