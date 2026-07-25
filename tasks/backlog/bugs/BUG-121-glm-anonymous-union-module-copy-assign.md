---
id: BUG-121
theme: none
depends_on: []
---
# BUG-121 — GLM anonymous-union copy-assignment fails to compile through a C++23 module boundary

## Goal
- Restore the CPU build of `tests/contract/runtime/Test.CameraModule.cpp` so the `full-cpu`,
  `ci-asan`, and `ci-ubsan` gates reach their test phase again.

## Non-goals
- Disabling, quarantining, or excluding `Test.CameraModule.cpp` to reach green. `AGENTS.md` §10
  forbids weakening a gate to get past a diagnosis.
- Changing `glm` version or pinning without understanding which side owns the defect.

## Context
Observed on `ci-linux-clang` / `full-cpu` and `ci-sanitizers` / `asan` during an unrelated
docs-and-tooling PR (#1027) whose diff contains no `.cpp`, `.cppm`, `.hpp`, CMake, or
`vcpkg.json` changes, so it cannot be the cause.

`clang++-20` rejects GLM's anonymous union when the implicit copy-assignment operator for
`glm::vec<3, float>` is first required inside a translation unit that reaches the type through a
module import chain:

```
In module 'Extrinsic.Runtime.Engine' imported from tests/support/RuntimeTestModule.hpp:10:
In module 'Extrinsic.RHI.Device'    imported from src/runtime/Runtime.Engine.cppm:10:
In module 'Extrinsic.RHI.CommandContext' imported from src/graphics/rhi/RHI.Device.cppm:10:
In module 'Extrinsic.RHI.Types'     imported from src/graphics/rhi/RHI.CommandContext.cppm:12:
external/vcpkg-installed/ci/x64-linux/include/glm/./ext/../detail/type_vec3.hpp:77:4:
  error: class member cannot be redeclared
   77 |    union { T x, r, s; };
note: while declaring the implicit copy assignment operator for
      '(anonymous union at .../type_vec3.hpp:77:4)'
tests/contract/runtime/Test.CameraModule.cpp:41:23:
note: in defaulted copy assignment operator for 'glm::vec<3, float>' first required here
   41 |   seed.Position = position;
note: previous definition is here
   77 |    union { T x, r, s; };
1 error generated.
ninja: build stopped: subcommand failed.
```

Both gates then fail downstream for the same root cause — no test phase runs, so
`aggregate_gate_timing.py` reports `missing phase report: .../phases/test.json` and the JUnit
upload warns `No files were found`. Those are symptoms, not separate defects.

Evidence:
- [`full-cpu`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/30155655723/job/89673065819)
  — cold cache, `--cache-state cold`, so this is not a stale-BMI artifact (rules out the
  `intrinsicengine-stale-build-triage` path).
- [`sanitizer-tests (asan)`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/30155655723/job/89673065900)
  — same shape under the `ci-asan` preset.

The failing edge is `[1892/2207]`, i.e. late in the build, so most of the tree compiles fine —
`glm::vec3` is only fatal where a *defaulted copy assignment* is instantiated after the type
arrives via module import. Nearby translation units that only construct or read `glm::vec3`
build clean, which is why this reproduces narrowly.

## Required changes
- [ ] Confirm the reproduction locally with `cmake --preset ci` and
      `cmake --build --preset ci --target IntrinsicTests` on clang-20, from a clean tree.
- [ ] Determine ownership: a clang-20 module/anonymous-union defect, a GLM defect, or a
      repository issue in how `glm` types cross the `Extrinsic.RHI.Types` module surface.
- [ ] Fix at the right layer. Candidates, cheapest first:
      - avoid the defaulted copy assignment at the call site (`seed.Position = position;` →
        explicit component assignment or construction) as a narrow unblock;
      - stop re-exporting `glm` types through the module interface and pass an engine-owned
        POD across the boundary;
      - include `glm` as a non-module header in the affected TU;
      - upstream/patch `glm` via an overlay port if the defect is GLM's.
- [ ] If the narrow call-site unblock is chosen, record it as a temporary shim per §13 with a
      removal task ID, since it does not address the module-boundary cause.

## Tests
- [ ] `cmake --build --preset ci --target IntrinsicTests` completes.
- [ ] `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passes.
- [ ] The `ci-asan` and `ci-ubsan` presets build and run their CPU cohort.
- [ ] A regression guard exists if a call-site workaround is used, so the pattern cannot return
      silently.

## Docs
- [ ] If `glm` types stop crossing a module surface, update `docs/architecture/` for the changed
      module boundary and refresh `docs/api/generated/module_inventory.md`.
- [ ] If a shim lands, note it in the owning task per §13.

## Acceptance criteria
- [ ] `full-cpu`, `ci-asan`, and `ci-ubsan` reach and pass their test phases.
- [ ] No gate was disabled, excluded, or quarantined to achieve it.
- [ ] The chosen layer for the fix is justified against the layering rules in `AGENTS.md` §4.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Excluding the affected test or gate to reach green.
