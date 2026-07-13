---
id: BUG-084
theme: G
depends_on: []
---
# BUG-084 — First configure can omit the default Vulkan backend

## Goal
- Make a clean preset configure and every immediate reconfigure produce the
  same backend target graph when the caller relies on the documented default
  `EXTRINSIC_BACKEND=Vulkan` selection.

## Non-goals
- No change to the documented `Auto|Null|Glfw` platform-backend truth table.
- No promotion of Vulkan runtime use beyond its existing two-key opt-in gate.
- No renderer, platform, or Vulkan implementation changes.

## Context
- Symptom: while verifying `ARCH-006` on 2026-07-14, the first clean `ci`
  configure omitted `ExtrinsicBackendsVulkan`; running the same configure a
  second time added `src_graphics_vulkan` and its dependent test build edges.
- Expected behavior: CMake target selection is a pure function of source,
  preset inputs, and host capabilities; an unchanged second configure must not
  change the selected backend or available targets.
- Impact: clean checkouts can build a smaller graph than warm build trees, so
  Vulkan compile coverage and backend-dependent tests vary with cache history.
- Root cause evidence: the root `CMakeLists.txt` adds
  `src/graphics/renderer` before `src/platform`; renderer backend selection
  reads `EXTRINSIC_BACKEND`, while `src/platform/CMakeLists.txt` defines its
  default and persists it in the cache only after the renderer decision. The
  `ci-vulkan` preset pins the value and is not the affected defaulting path.

## Required changes
- [ ] Define the shared platform/backend selection inputs before any consuming
      layer is configured, with one canonical owner for their defaults and
      cache metadata.
- [ ] Remove the late platform-subdirectory default that makes the renderer
      target graph depend on whether a prior configure populated the cache.
- [ ] Keep explicit preset and command-line overrides authoritative.

## Tests
- [ ] Add a CMake/tooling regression that configures from an empty cache and
      then reconfigures unchanged, proving the selected backend and relevant
      target presence are identical.
- [ ] Cover the explicit `EXTRINSIC_BACKEND=Vulkan` and documented headless
      Null-selection paths without requiring a physical GPU.

## Docs
- [ ] Update CMake/platform ownership documentation if the canonical option
      definition moves, while preserving the current backend truth table.
- [ ] Update this bug index and the retirement log when verified.

## Acceptance criteria
- [ ] Clean and warm `ci`/`dev` configures expose the same backend targets for
      identical inputs.
- [ ] Explicit backend overrides still win, and strict layering plus default
      CPU verification remain green.
- [ ] The regression fails against the pre-fix subdirectory ordering/default
      behavior.

## Verification
```bash
python3 tests/regression/tooling/Test.CMakeBackendSelection.py -v
cmake --preset ci --fresh
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Hiding the inconsistency by configuring twice in CI or setup scripts.
- Making Vulkan operational by default in runtime.
- Shipping the ordering fix without a clean-cache regression.
