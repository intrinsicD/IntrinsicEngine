---
id: BUG-121
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
owner: "codex-root"
branch: "agent/framework24-product-convergence-goal"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-13T11:54:23Z"
evidence_skip_reason: "Task-only duplicate-owner reconciliation: the compiler defect remains open under BUG-157 and no source, test, build, or gate behavior changes here."
template: micro
contract_schema: 1
contracts: [repo.task-contract-discovery]
contract_review: "Retiring this duplicate changes only authoritative task discovery: BUG-157 becomes the one open owner. It changes no engine, module, method, publication, config, runtime, UI, or backend contract."
---
# BUG-121 — GLM anonymous-union copy-assignment fails through a C++23 module boundary

## Status

- Closed on 2026-08-13 as an exact duplicate of `BUG-157`; the underlying
  compiler defect is not fixed by this task.
- `BUG-157` is the sole authoritative implementation and verification owner.
- Closure PR: #1030; there is no defect-fix commit in this task.

## Goal

- Remove duplicate ownership of the Clang 20/GLM anonymous-union compilation
  failure while preserving its original evidence under one current task.

## Non-goals

- No source, test, CMake, toolchain, CI-gate, engine, method, renderer, UI, or
  benchmark behavior change.
- No claim that the compiler defect is fixed.

## Context

- `BUG-121` and `BUG-157` name the same failure: Clang 20 rejects
  `glm/detail/type_vec3.hpp:77` while compiling
  `tests/contract/runtime/Test.CameraModule.cpp:41` through the imported RHI
  module chain.
- `BUG-157` contains the newer local and hosted reproductions, uses the current
  workflow schema, and is assigned to the Framework24 convergence gate.
- Keeping both open made work selection ambiguous and allowed independent
  agents to create a third duplicate owner.

## Required changes

- [x] Retire this duplicate without claiming the underlying defect is fixed.
- [x] Preserve `BUG-157` as the only open owner and retain the original
      diagnosis there.

## Tests

- [x] Strict task validation resolves exactly one open owner for this failure.

## Docs

- [x] Move the category-index entry to Verified / Closed and regenerate the
      session brief.

## Acceptance criteria

- [x] `BUG-121` no longer appears as open work.
- [x] `BUG-157` explicitly owns implementation and full CPU/sanitizer recovery.
- [x] No method or production implementation is changed.

## Verification

```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
```

## Forbidden changes

- Claiming the Clang 20/GLM defect is fixed by this bookkeeping closure.
- Opening another task for the same source line and diagnostic.
