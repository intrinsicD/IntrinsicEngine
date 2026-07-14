---
id: BUG-084
theme: G
depends_on: []
---
# BUG-084 — TransformSyncSystem contract test uses an unqualified test namespace

## Status
- Status: retired (2026-07-14).
- Owner: Codex.
- Branch: `codex/arch-006-completion` (separate blocker commit).
- Closure: `CPUContracted`; the focused and aggregate sanitizer-enabled gates
  compile and the default CPU-supported test selection passes.

## Goal
- Restore compilation of the sanitizer-enabled `IntrinsicTests` aggregate by
  naming the existing mock RHI device from its declared namespace.

## Non-goals
- No production transform-sync or renderer behavior changes.
- No expansion of the TransformSyncSystem test scenarios.

## Context
- Symptom: `cmake --build --preset ci --target IntrinsicTests` fails while
  compiling `Test.TransformSyncSystem.cpp` because `Tests::MockDevice` is not
  declared in the global namespace.
- Expected behavior: the contract target compiles against
  `Extrinsic::Tests::MockDevice`, as the neighboring graphics contracts do.
- Impact: the default CPU-supported gate cannot reach test execution.
- Evidence: the failing source is unchanged from `origin/main`; ARCH-006 does
  not touch this test or its graphics dependencies.

## Required changes
- [x] Fully qualify both `MockDevice` declarations in the existing contract.

## Tests
- [x] Build the focused graphics CPU contract target under the `ci` preset.
- [x] Build `IntrinsicTests` and run the default CPU-supported CTest gate.

## Docs
- [x] Record the discovered gate failure and verified repair in the bug index
  and retirement log; no architecture documentation changes are required.

## Acceptance criteria
- [x] The TransformSyncSystem contract compiles under Clang 23 with ASan/UBSan.
- [x] The fix changes no production source and introduces no layering issue.

## Evidence
- `IntrinsicGraphicsContractCpuTests` built after the two declarations were
  qualified as `Extrinsic::Tests::MockDevice`.
- `IntrinsicTests` built with the `ci` preset on Clang 23 with ASan/UBSan.
- The default CPU-supported CTest gate passed 3,698/3,698, including the nine
  headless `RuntimeSandboxAcceptance` tests that originally could not run.
- The source was identical on `origin/main` before repair; no production file
  changed for this bug.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Changing transform-sync behavior to compensate for a test-only namespace
  error.
- Weakening or excluding the failing aggregate gate.
