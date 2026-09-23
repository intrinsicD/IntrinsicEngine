---
id: BUG-215
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive test import repair with clean-runner compiler failure and focused build/test evidence
contract_schema: 1
contracts: []
contract_review: Explicit owning-module import in an existing test only; no production API, dependency edge or assertion change.
---
# BUG-215 — Import the resource-ID comparison owner in the debug-view test

## Goal

Repair the pre-existing Clang 20 clean-build failure exposed by
[PR #1044](https://github.com/intrinsicD/IntrinsicEngine/pull/1044).
`Test.Graphics.DebugViewSystem.cpp` compares `FrameResourceId` values through
GoogleTest but imports only the debug-view and recipe modules. Explicitly
import `Extrinsic.Graphics.RenderGraph`, which re-exports the Resources
partition owning the comparison operators. Keep all four resource-ID
assertions and production module exports unchanged.

## Acceptance criteria

- [x] Retain the clean Clang 20 compiler diagnostic and import its existing owner.
- [x] Build the affected test target with the complete local Clang 20 toolchain.
- [x] Execute the focused debug-view tests without changing their assertions.

## Verification

```bash
cmake --preset ci -B build/ci-clang20 -DCMAKE_C_COMPILER=/usr/bin/clang-20 -DCMAKE_CXX_COMPILER=/usr/bin/clang++-20 -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-20 -DINTRINSIC_PLATFORM_BACKEND=Null -DINTRINSIC_HEADLESS_NO_GLFW=ON -DVCPKG_MANIFEST_INSTALL=OFF
cmake --build build/ci-clang20 --target IntrinsicGraphicsRendererCpuUnitTests -j2
ctest --test-dir build/ci-clang20 --output-on-failure -R '^GraphicsDebugViewSystem\.' --no-tests=error --timeout 60 -j1
```

The existing isolated Clang 20 tree reuses the already resolved manifest
dependencies without reprovisioning the shared dependency directory. This is
focused compiler evidence; the separate full feature gates remain source-bound
to their recorded implementation revision.

## Completion

Retired 2026-09-23 at the test build-maintenance endpoint.
PR/commit: [PR #1044](https://github.com/intrinsicD/IntrinsicEngine/pull/1044),
the enclosing owning-module import repair commit.

The complete Clang 20 toolchain configured the ci preset in its isolated tree,
built IntrinsicGraphicsRendererCpuUnitTests and passed all five focused cases.
[CTest output](../evidence/BUG-215/ctest.log) and the
[actual Opus 5.5 review](../evidence/BUG-215/claude-review.md) retain the proof;
the review preceded build completion, which the subsequent build/test logs close.
The exact failing CI compile is retained compressed. Only the missing test import
changed; production exports, comparison operators and assertions are untouched.
Remote CI reruns after push.
