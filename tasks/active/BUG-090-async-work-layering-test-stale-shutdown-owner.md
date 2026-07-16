---
id: BUG-090
theme: G
depends_on:
  - RUNTIME-165
maturity_target: CPUContracted
---
# BUG-090 — Async-work layering test asserts stale shutdown call spelling

## Status

- In progress on 2026-07-16; owner: Codex; branch:
  `codex/arch-006-completion`.
- Fix and verification complete: the exact test was reproduced with only the
  two documented missing spellings, then passed 1/1 after the correction; the
  complete source-layering selection passed 24/24. Next gate: self-review and
  technical commit.

## Goal
- Restore the `RuntimeEngineLayering` source contract by asserting the current
  Engine shutdown-hook delegation to `AsyncWorkService`.

## Non-goals
- No `AsyncWorkService`, Engine shutdown, test-label, or frame-order behavior
  change.
- No broad rewrite of source-reading layering contracts.

## Context
- Symptom: the direct `^RuntimeEngineLayering\.` CTest selection fails
  `AsyncWorkServiceKeepsStreamingAndDerivedJobOwnershipOutOfEngine` because it
  searches `Runtime.Engine.cpp` for
  `m_AsyncWorkService.ShutdownAndDrain()` and `m_AsyncWorkService.Reset()`.
- Expected behavior: the contract should recognize the current
  `ShutdownHooks::AsyncWork` delegation, which calls
  `AsyncWork.ShutdownAndDrain()` and `AsyncWork.Reset()` after Engine passes
  `m_AsyncWorkService` into the hook.
- Impact: one of 24 source-layering tests is red when its containing opt-in
  integration binary is selected directly. The default CPU gate excludes that
  binary by label, so the stale assertion can remain hidden.
- Evidence: both stale strings are absent from pre-`RUNTIME-167` `HEAD`, while
  the current delegated spellings are present at the same shutdown lifecycle
  boundary. The failure is therefore pre-existing test drift, not a frame-loop
  privatization regression.

## Required changes
- [x] Replace only the two stale member-qualified assertions with the current
      shutdown-hook delegation spellings.
- [x] Preserve the positive Engine ownership/delegation checks and the negative
      raw streaming/derived-job ownership checks.

## Tests
- [x] Reproduce the exact failing test before the fix and pass it afterward.
- [x] Run the complete `RuntimeEngineLayering` selection.

## Docs
- [x] Keep the bug index, task lifecycle record, and session brief synchronized.

## Acceptance criteria
- [x] The direct source-layering selection passes without changing production
      code or weakening an ownership assertion.
- [x] Strict task, state-link, documentation-link, and diff checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeEngineLayering\.AsyncWorkServiceKeepsStreamingAndDerivedJobOwnershipOutOfEngine$' \
  --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeEngineLayering\.' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Changing Engine/async-work shutdown behavior to satisfy the stale text check.
- Relabeling, skipping, or weakening the failing test.
- Folding unrelated frame-loop assertions into this bug fix.
