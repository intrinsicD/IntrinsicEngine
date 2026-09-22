---
id: BUG-176
theme: J
depends_on: []
workflow_schema: 1
template: micro
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive delegated fix; evidence is the reviewed diff and executed regression/CTest gates."
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Build-directory discovery coordination has no current catalog contract. This investigation must preserve exact CTest registration and selection."
---
# BUG-176 — Concurrent CTest discovery can duplicate generated registrations

## Completion — 2026-09-22

Retired (tooling correctness endpoint). Implementation commit: `84384187972da72f9398b2e8077b8771f2020db0`.
All acceptance criteria are verified below; no blocking dependencies or deferred
implementation remain. No GPU/backend capability or performance claim is made.

## Goal
- Prevent concurrent local test discovery from corrupting the same build
  directory's generated PRE_TEST registration files.

## Non-goals
- No deduplication that hides real duplicate source registrations, reduced test
  selection, or mutation of failed verification receipts.

## Context
- During METHOD-040, a full CTest run and registry-reading validators were
  launched concurrently after CMake regeneration. CMake 3.28 PRE_TEST discovery
  writes generated `*_tests.cmake` files. Two files acquired duplicate add_test
  entries while their binaries listed the cases only once: CoreWrapperUnit and
  GraphicsContractCpu. The CPU run executed 4,791 physical entries for 4,295
  unique selected names; strict routing/parity rejected the duplicates.
- The raw metadata diagnosis is retained at
  `tasks/evidence/METHOD-040/experiments/duplicate-discovery.json`; generated
  corrupt files were copied to `/tmp/method040-duplicate-discovery` before removal.
- Serial regeneration from unchanged binaries restored 4,295 physical/unique
  selected entries, strict CPU routing, and exact grouped/discovered logical
  parity. The source test set did not change. This corrects local build metadata;
  no repository coordination guard is implemented yet.

## Required changes
- [x] Reproduce concurrent discovery in an isolated synthetic CMake project and
      inspect the generated-file write sequence.
- [x] Select a bounded coordination mechanism for repository-owned discovery
      callers, or document an upstream fix with a supported CMake migration.
- [x] Keep discovery serialized until that mechanism exists; show-only CTest
      commands are potentially mutating when registration files are stale.

## Tests
- [x] Concurrent discovery yields exactly one registration per source case.
- [x] Real duplicate source cases still fail the strict routing validator.

## Docs
- [x] Document the ownership and scope of discovery synchronization.

## Acceptance criteria
- [x] Deterministic reproduction fails before and passes after the coordination fix.
- [x] Individual/grouped registration and complete CPU selection are unchanged.

## Verification
```bash
python3 tests/regression/tooling/Test.ConcurrentCTestDiscovery.py -v
python3 tests/regression/tooling/Test.TestGateRouting.py --self-test
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-asan -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicCpuTests
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicCpuTests
python3 tests/regression/tooling/Test.GroupedCTestParity.py registration --individual-build-dir build/ci --grouped-build-dir build/ci-asan
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Hiding duplicates by filtering the registry, dropping tests, or retrying
  concurrent writers without preserving their failure evidence.

## Implementation plan
- Reproduce chunked WRITE/APPEND corruption with controlled competing CTest processes in a synthetic project.
- Extend the existing TEST_INCLUDE_FILES registration owner with build-local acquire/release includes; preserve GoogleTest discovery and canonical property fixups.
- Verify concurrent registries, nested CTest, duplicate rejection, and individual/grouped CPU selection; review with Claude before retirement.

## Reuse decision
- `intrinsic_test_executable` in `tests/CMakeLists.txt` owns every PRE_TEST GoogleTest registration; its final property-fixup include consumes the discovered lists. No existing discovery lock was found. Keep both owners and bracket their includes instead of changing Python readers or copying GoogleTest.
- The contract catalog has no test-discovery synchronization contract. No engine API or layer changes are involved.

## Verification results — 2026-09-22
- Deterministic regression: 3/3 pass. The unlocked control forces both stale readers through discovery, orders A WRITE → B WRITE → both APPEND, and observes duplicated names. The guarded run verifies the production lock is held while a second CTest starts, then returns exactly 600 cases with unchanged filters/properties. Nested CTest completes; genuine duplicate source names remain visible.
- `cmake --preset ci` and `cmake --build --preset ci --target IntrinsicTests`: pass, Clang 23.
- Canonical exclusion-only CPU gate: 4,880 selected entries, zero failures; existing `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl` reports Skipped. Runtime 184.93 seconds.
- Final diagnostic-only update: synthetic 3/3 pass, canonical configure/build pass, focused `ctest --test-dir build/ci --output-on-failure -R '^Core' --timeout 60` passes all 280 entries.
- Routing self-tests: 19/19 pass, including duplicate rejection; live CPU routing: 29 producers, 4,783 logical cases, 364 source files.
- `cmake --preset ci-asan -DINTRINSIC_GROUP_PURE_CTEST=ON` and `cmake --build --preset ci-asan --target IntrinsicCpuTests`: pass. Registration parity: 4,783 identical logical cases, 4,880 individual versus 3,231 grouped physical CPU records. This is registration evidence, not an ASan execution claim.
- Test-build aggregate self-tests 8/8, workflow concurrency 20/20, workflow routing 11/11, ccache workflow 23/23, CI timing 20/20: pass. Test layout, root hygiene, workflow naming and task policy: pass.

## Diagnosis and review decisions
- Confirmed chunked generated-file WRITE/APPEND interleaving in CMake 3.28.3. The synthetic source emits each case once; both readers include only once; controlling competing writers reproduces the duplicates, ruling out source duplication and repeated inclusion as the cause of this reproduction.
- The installed GoogleTest implementation stays unchanged. `TEST_INCLUDE_FILES` is the existing owner hook; lock acquisition precedes freshness checks and release follows canonical property fixups. Explicit release prevents a nested-CTest deadlock.
- Claude Sonnet initially proposed a whole-process wrapper; source inspection and Claude Opus review rejected that premise and confirmed the registration hook. Opus requested a clearer timeout diagnostic, now implemented. Its proposed 300-second wait was not adopted because repository registry readers already terminate after 120 seconds. The documented 90-second contention budget fails closed within that deadline; it is not a guarantee for arbitrarily slow or suspended competing discovery.
- Full test execution, result reports and CMake/build mutation remain subject to one-writer ownership. This fix coordinates registration loading only, and deliberately does not repair previously corrupt cached files or filter duplicate source registrations.
- Final Claude Sonnet review and independent Codex subagent review: no blocking findings. The synthetic probe intentionally fails closed if a future CMake version changes its instrumented internal flush implementation.
