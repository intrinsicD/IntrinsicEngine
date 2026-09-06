---
id: BUG-176
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Build-directory discovery coordination has no current catalog contract. This investigation must preserve exact CTest registration and selection."
---
# BUG-176 — Concurrent CTest discovery can duplicate generated registrations

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
- [ ] Reproduce concurrent discovery in an isolated synthetic CMake project and
      inspect the generated-file write sequence.
- [ ] Select a bounded coordination mechanism for repository-owned discovery
      callers, or document an upstream fix with a supported CMake migration.
- [ ] Keep discovery serialized until that mechanism exists; show-only CTest
      commands are potentially mutating when registration files are stale.

## Tests
- [ ] Concurrent discovery yields exactly one registration per source case.
- [ ] Real duplicate source cases still fail the strict routing validator.

## Docs
- [ ] Document the ownership and scope of discovery synchronization.

## Acceptance criteria
- [ ] Deterministic reproduction fails before and passes after the coordination fix.
- [ ] Individual/grouped registration and complete CPU selection are unchanged.

## Verification
```bash
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicCpuTests
python3 tests/regression/tooling/Test.GroupedCTestParity.py registration --individual-build-dir build/ci --grouped-build-dir build/ci-asan
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Hiding duplicates by filtering the registry, dropping tests, or retrying
  concurrent writers without preserving their failure evidence.
