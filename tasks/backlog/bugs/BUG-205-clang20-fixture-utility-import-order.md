---
id: BUG-205
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive minimum-toolchain regression; deterministic before/after compiler evidence retained
contract_schema: 1
contracts: []
contract_review: Test-only include ordering; no engine API, ownership or catalog contract changes.
---
# BUG-205 — Clang 20 rejects utility declarations after fixture imports

## Goal
Restore the editor fixture producer on the supported minimum compiler. A fresh,
cache-disabled ci-derived Clang20 Null/headless build during BUILD-011 rejects
libstdc++14's deleted `std::as_const` overload with a misplaced `lifetimebound`
attribute. The unchanged EditorFeatureTestContext.cpp and SandboxEditorJobHarness.cpp
include utility after their import-bearing fixture headers. The editor-context compiler module map contains none of the ten
config interfaces changed by BUILD-011. This is a separate pre-existing producer
failure, not evidence against the codec relocation.

## Acceptance criteria
- [x] Exact Clang20 command reproduces the failure before and compiles after moving
      the existing standard includes before the import-bearing headers. No body or
      signature changes; no suppressed diagnostics or altered test selection.
- [ ] Runtime-contract target links and focused checks execute under Clang20;
      canonical ci rebuild and relevant CPU checks pass.
- [ ] Keep the correction in both BUILD-011 measurement arms and commit separately.

## Verification
```bash
cmake --build /dev/shm/intrinsic-build011-clang20 --target IntrinsicRuntimeContractTests --parallel 4
/dev/shm/intrinsic-build011-clang20/bin/IntrinsicRuntimeContractTests --gtest_filter='*Config*:*Registration*:*NormalEstimation*:*Density*:*Spacing*:*Outlier*:*Bilateral*:*Keypoint*:*Descriptor*:*PointConstruction*'
cmake --build --preset ci --target IntrinsicTests --parallel 4
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
The Clang20 tree was freshly configured from `ci`, explicit Clang20/compiler/scanner,
Null/headless, cache and launcher disabled, preinstalled vcpkg without installation.
