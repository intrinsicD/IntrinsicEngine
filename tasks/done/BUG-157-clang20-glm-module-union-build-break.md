---
id: BUG-157
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive delegated session; the evidence is the reviewed diff, the fresh Clang 20 and Clang 23 IntrinsicTests builds and default CPU gates recorded below, the standalone Clang 20/22/23 reproducers, and the hosted CI run on main."
owner: "claude-fable"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-03T19:09:30Z"
contract_schema: 1
contracts: []
contract_review: "The correction adds one directory-scope glm configuration definition, renames glm colour accessors mechanically, and imports one module in one test TU. It changes no engine layer boundary, geometry element-domain source, property publication, method integration, control surface, or agent-workflow contract, and leaves the documented minimum Clang major at 20."
---
# BUG-157 — Clang 20 fails IntrinsicTests on glm anonymous-union redeclaration

## Status

- Closed 2026-09-03. Sole authoritative owner for the Clang 20/GLM failure at
  `tests/contract/runtime/Test.CameraModule.cpp:41`; the local Clang 20
  reproduction found one more Clang-20-only failure in
  `tests/contract/graphics/Test.RendererFrameLifecycle.cpp` with a different
  mechanism, recorded and corrected here rather than in a parallel task.
- Historical duplicate `BUG-121` was closed without a fix on 2026-08-13; the
  proposed `BUG-162` duplicate was discarded before commit.
- No compiler or test-source repair is part of `BUG-161`.

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

### Diagnosis (2026-09-03)

- Every `ci-linux-clang` run on `main` since 2026-08-05 was red; the 2026-09-03
  run `33776061050` failed in "Build required CPU cohort" on exactly this
  diagnostic (Ubuntu noble-updates `clang-20 1:20.1.2-0ubuntu1~24.04.3`).
- Local reproduction on `/usr/bin/clang++-20` (20.1.2) with a `ci`-preset tree
  (`build/ci-clang20`, `-k 0`) fails **two** TUs, everything else compiles:
  1. `tests/contract/runtime/Test.CameraModule.cpp` — the glm anonymous-union
     redeclaration above. Mechanism: llvm/llvm-project PR #155948
     "[C++20][Modules] Fix merging of anonymous members of class templates"
     (merged 2025-09-19, commit `a2efa7ab207d`). A `ClassTemplateSpecializationDecl`
     skeleton created by glm's `typedef vec<3, float, defaultp> vec3;` is
     imported from a BMI but instantiated locally, so anonymous-member
     numbering never happens and the anonymous-union `FieldDecl`s from a
     second module lineage (`Extrinsic.RHI.Types` versus
     `Extrinsic.Graphics.CameraSnapshots`, which do not import each other) fail
     to merge; the first TU that requires `vec<3, float>`'s defaulted copy
     assignment then sees the union members twice. The fix commit is an
     ancestor of `llvmorg-22.1.0` and of neither `llvmorg-20.1.8` nor
     `llvmorg-21.1.8`.
  2. `tests/contract/graphics/Test.RendererFrameLifecycle.cpp:6222` and `:1003`
     — `invalid operands to binary expression ('const FramePassId' and
     'const FramePassId')`. `FramePassId` and its exported `operator==`/`!=`
     live in partition `Extrinsic.Graphics.RenderGraph:Pass`, re-exported by
     `Extrinsic.Graphics.RenderGraph`; the test reached the type only through
     `Extrinsic.Graphics.FrameRecipe`/`Renderer`, which `import` (not
     `export import`) that module. C++20 [basic.lookup.argdep]/4.3 requires
     ADL to find exported declarations attached to the associated entity's
     module even when that module is not imported; Clang 20's module-level
     lookup rewrite (llvm #90154) regressed this family (see llvm #133720,
     "[Regression:20]", fixed by `ac2b51e6` on 2025-09-18, also LLVM 22 only).
     `Test.FrameRecipeContract.cpp` compiles on Clang 20 because it imports
     `Extrinsic.Graphics.RenderGraph` directly.
- Only glm contributes anonymous unions to this tree (`src/` has none), so the
  first defect class is confined to glm vector types; glm's quaternion uses
  plain members on Clang without `-fms-extensions`.

### Correction chosen

- Keep Clang 20 as the documented minimum. `GLM_FORCE_XYZW_ONLY` is defined at
  directory scope in the root `CMakeLists.txt`, so glm declares plain
  `x, y, z, w` members with no anonymous unions and the merge defect cannot
  trigger in any TU. The definition is global rather than an `IntrinsicConfig`
  usage requirement because 224 of the 477 glm-including TUs (all of
  `src/geometry`) do not link `IntrinsicConfig`, and every TU must see one
  glm class layout. The 137 `.r/.g/.b/.a` accessor sites the compiler then
  rejected (14 files: geometry IO, renderer reconstruction/colormap/upload
  helpers, and their tests) were renamed to `.x/.y/.z/.w` by a compile-driven
  script; the diff is a pure token substitution.
- `Test.RendererFrameLifecycle.cpp` now imports
  `Extrinsic.Graphics.RenderGraph`, the module that exports the operators it
  compares with, which is also the explicit-seam reading of the test contract.
- Rejected alternatives: raising the minimum to Clang 22 would need
  `apt.llvm.org` in seven workflows (Ubuntu 24.04 ships no
  `llvm-toolchain-22`), a contract edit, and would drop Clang 20/21 hosts;
  a TU-local arrangement for the glm defect is not robust because the
  trigger depends on which module lineages a TU happens to import.
- Removal condition: the define may be dropped once the minimum supported
  Clang is 22 or newer; `docs/build-troubleshooting.md` records both defects.

## Required changes

- [x] Reproduce on a clean Clang-20 host and capture the exact failing TU
      set (whether more than `Test.CameraModule.cpp` triggers it): two TUs,
      recorded above.
- [x] Decide the correction: a Clang-20-compatible arrangement of glm
      textual/module visibility for the affected TU(s), a toolchain minimum
      bump with preset/docs updates, or an upstream-pinned workaround with a
      removal condition — the global `GLM_FORCE_XYZW_ONLY` arrangement plus
      one explicit module import, with the removal condition recorded.
- [x] Keep `check_layering` and module-ownership rules intact in whichever
      correction is chosen.

## Tests

- [x] A full `IntrinsicTests` build on the documented minimum toolchain
      completes, or the documented minimum is corrected in the same change.
- [x] The default CPU-supported gate passes on that toolchain.

## Docs

- [x] Update `AGENTS.md`/setup docs if the minimum supported Clang changes
      (unchanged; `docs/build-troubleshooting.md` documents both defects and
      the arrangement instead).
- [x] Record the resolved root cause and chosen correction in this task
      before retirement.

## Acceptance criteria

- [x] The documented minimum toolchain and the actual buildability of
      `IntrinsicTests` agree.
- [x] The failing TU set has a recorded explanation tied to the Clang
      modules defect class rather than a speculative workaround.

## Verification

```bash
clang-20 --version
cmake --preset ci -B build/ci-clang20 --fresh \
  -DCMAKE_C_COMPILER=/usr/bin/clang-20 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-20 \
  -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-20 \
  -DVCPKG_MANIFEST_INSTALL=OFF -DVCPKG_INSTALLED_DIR=$PWD/external/vcpkg-installed/ci \
  -DINTRINSIC_BUILD_SANDBOX=ON
cmake --build build/ci-clang20 --target IntrinsicTests IntrinsicCpuTests ExtrinsicSandbox
ctest --test-dir build/ci-clang20 --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Results (verified 2026-09-04):

- Clang 20.1.2 fresh cache-disabled tree
  `/tmp/intrinsic-ci-clang20-final`: `IntrinsicTests`, `IntrinsicCpuTests`,
  and `ExtrinsicSandbox` built; the default CPU gate passed 4,263/4,263 tests
  with one expected capability skip.
- Clang 23 `build/ci`: `IntrinsicTests` built; the default CPU gate passed
  4,263/4,263 tests with one expected capability skip.
- Standalone reproducers (no glm, no gtest) for both defects failed on
  clang++-20 and passed on clang++-22 and clang++-23 during diagnosis.
- Layering, test-layout, doc-link, and task-policy checks pass.

### Clean-workshop review (2026-09-04)

| Row | Result | Evidence |
| --- | --- | --- |
| 1. Promoted layer imports | pass | Strict layering check passed with no allowlist entries. |
| 2. CMake target links | pass | No target-link edge changed. |
| 3. Public downward types | n/a | No public module surface changed. |
| 4. Renderer ownership | n/a | Existing renderer code received token-only accessor substitutions. |
| 5. Typed pass IDs | n/a | No pass identity or pass registration changed. |
| 6. Recipe dependencies | n/a | No recipe or resource dependency changed. |
| 7. Maturity follow-up | n/a | This restores an existing build contract; no scaffold was introduced. |
| 8. Temporary exceptions | n/a | No allowlist entry or compatibility shim was added. |

## Forbidden changes

- Marking the affected tests as skipped or quarantined to hide the build
  break.
- Mixing this toolchain repair into the BUG-156 geometry correction.
- Opening a parallel task for the same `Test.CameraModule.cpp:41` diagnostic;
  append new evidence and hypotheses here instead.

## Maturity

- Completed: 2026-09-03.
- Commit: mechanical accessor rename `d538639c8`; glm configuration,
  test import, and troubleshooting docs `c1c25e387`; retirement metadata
  follows in the next commit.
- Target: restore the existing full-test build's health on the documented
  minimum toolchain; no new feature surface.
- Actual: the full `IntrinsicTests`, `IntrinsicCpuTests`, and
  `ExtrinsicSandbox` targets build on Clang 20.1.2 and Clang 23, and the
  default CPU gate passes on both.
