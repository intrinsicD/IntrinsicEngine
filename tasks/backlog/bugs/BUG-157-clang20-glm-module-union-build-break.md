---
id: BUG-157
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "codex-root"
branch: "agent/framework24-product-convergence-goal"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-13T12:06:10Z"
contract_schema: 1
contracts: []
contract_review: "The repair is confined to a test consumer: Test.CameraModule.cpp no longer textually includes glm while also importing module-owned glm types. It changes no reusable module, engine, method, config, runtime, UI, publication, or backend contract."
---
# BUG-157 — Clang 20 fails IntrinsicTests on glm anonymous-union redeclaration

## Status

- Sole authoritative owner for the Clang 20/GLM failure at
  `tests/contract/runtime/Test.CameraModule.cpp:41`.
- Historical duplicate `BUG-121` was closed without a fix on 2026-08-13; the
  proposed `BUG-162` duplicate was discarded before commit.
- No compiler or test-source repair is part of `BUG-161`.
- The candidate repair removes the redundant textual glm declaration set from
  this one test consumer. Test inputs and exact position assertions now use the
  exported `CameraViewInput` members without naming `glm::vec3`; no runtime or
  method implementation changes.
- Clang 23 locally compiles the focused test object and all 11 `CameraModule*`
  contracts pass. A clean hosted Clang 20 build remains the authoritative
  regression check.

## Goal

- Restore a green full `IntrinsicTests` build on the repository's documented
  minimum toolchain (Clang 20), or correct the documented minimum if the
  toolchain genuinely cannot compile the current module/glm combination.

## Non-goals

- No suppression of the error by weakening `-Werror`-class settings globally.
- No glm fork or vendored patch without a pinned upstream justification.
- No unrelated test rewrites beyond the minimal unit(s) that trigger the
  defect.

## Context

- On a Clang-20-only host (`ci` preset, fresh configure and fresh build tree,
  vcpkg-pinned glm), `cmake --build --preset ci --target IntrinsicTests`
  deterministically fails while compiling
  `tests/contract/runtime/Test.CameraModule.cpp`:
  `glm/detail/type_vec3.hpp:77: error: class member cannot be redeclared`
  (`union { T x, r, s; };`), raised while declaring the implicit copy
  assignment operator for `glm::vec<3, float>` at
  `Test.CameraModule.cpp:41` (`seed.Position = position;`). The TU sees glm
  both textually and through the imported module chain
  `Extrinsic.Runtime.Engine` → `Extrinsic.RHI.Device` →
  `Extrinsic.RHI.CommandContext` → `Extrinsic.RHI.Types`.
- The same failure reproduced in a second build tree (`ci-release`,
  incremental) during BUG-156 verification, before any local change to
  runtime or RHI sources; the file predates the BUG-156 branch and is not
  touched by it. Geometry-layer targets (`IntrinsicGeometryTests`,
  `IntrinsicCurvatureCorpusProbe`) build and pass on the same host, so the
  break is specific to TUs that combine textual glm inclusion with the RHI
  module chain.
- GitHub-hosted CI installs `clang-20`/`clang-tools-20`
  (`.github/workflows/ci-linux-clang.yml`). After `BUG-161` fixed the earlier
  shallow-checkout validator failure, PR 1030 reproduced this exact compile
  diagnostic in full CPU job `94431455381`, ASan job `94431455564`, and UBSan
  job `94431455475`. PR 1028's ASan job `94069394793` had already failed at the
  same source line and import chain before the Framework24 policy branch, so
  the policy change did not introduce it. Local development machines on
  Clang 23 do not reproduce it, which matches the known Clang C++20 modules
  defect class around anonymous-union members that was fixed after Clang 20.
- `AGENTS.md` documents Clang 20 as the minimum supported major version, so
  either the minimum claim or the build is currently wrong.

## Required changes

- [ ] Reproduce on a clean Clang-20 host and capture the exact failing TU
      set (whether more than `Test.CameraModule.cpp` triggers it).
- [x] Decide the correction: a Clang-20-compatible arrangement of glm
      textual/module visibility for the affected TU(s), a toolchain minimum
      bump with preset/docs updates, or an upstream-pinned workaround with a
      removal condition. Chosen correction: keep glm owned by the imported
      module surfaces and stop redeclaring it textually in this test TU.
- [x] Keep `check_layering` and module-ownership rules intact in whichever
      correction is chosen.

## Tests

- [ ] A full `IntrinsicTests` build on the documented minimum toolchain
      completes, or the documented minimum is corrected in the same change.
- [ ] The default CPU-supported gate passes on that toolchain.

## Docs

- [ ] Update `AGENTS.md`/setup docs if the minimum supported Clang changes.
- [ ] Record the resolved root cause and chosen correction in this task
      before retirement.

## Acceptance criteria

- [ ] The documented minimum toolchain and the actual buildability of
      `IntrinsicTests` agree.
- [ ] The failing TU set has a recorded explanation tied to the Clang
      modules defect class rather than a speculative workaround.

## Verification

```bash
clang-20 --version
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

# Local focused feedback (Clang 23; not minimum-toolchain evidence)
cmake --build --preset ci --target RuntimeContractTestObjs
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R '^CameraModule' --timeout 60
```

## Forbidden changes

- Marking the affected tests as skipped or quarantined to hide the build
  break.
- Mixing this toolchain repair into the BUG-156 geometry correction.
- Opening a parallel task for the same `Test.CameraModule.cpp:41` diagnostic;
  append new evidence and hypotheses here instead.

## Maturity

- Target: restore the existing full-test build's health on the documented
  minimum toolchain; no new feature surface.
