---
id: BUG-200
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive CPU completion fix; regression and source diff retain evidence.
contract_schema: 1
contracts: []
contract_review: Existing runtime completion and undo semantics apply; no geometry-input, property-publication or CPU/GPU coherence contract changes.
---
# BUG-200 — Queued denoise rejects a valid NoChange completion

## Goal
Treat a completed denoise with no moved vertices as a published NoChange result,
including its useful diagnostic, without creating an undo entry.

## Evidence
The new direct/queued comparison under UI-037 found the existing worker at
`9da1ffdd5` sets NoChange while `PublishMeshDenoiseCpuJob` rejects every result
whose Succeeded() is false. Thus the valid zero-movement branch below that guard
is unreachable for queued denoise. The all-boundary triangle produced an empty
message and a job publication error; direct execution explained all three
vertices were pinned. Local failure log: `/tmp/intrinsic-topology-reuse/focused.log`.

## Acceptance criteria
- [x] Accept NoChange at the completion gate while preserving actual failures.
- [x] Exactly one published terminal callback with matching direct/queued message,
      unchanged geometry and no history edit; existing failure/stale tests pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R '^SandboxEditorUi\.(Mesh(Denoise|Remesh|Subdivide|Simplify)|FailedQueuedMesh|Topology)' --timeout 60 --no-tests=error
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 --no-tests=error
```

## Completion — 2026-09-16
Retired as a tested CPU completion repair, with no engine maturity change.
PR/commit: enclosing topology computation reuse commit (implementation and
retirement). The publisher admits NoChange and retains its existing zero-movement
completion path; actual failures still reject publication. The comparison test
checks Published state, exactly one callback, matching explanation, unchanged
geometry and no undo entry. No deferred work.

Canonical `ci` configure and `IntrinsicTests` build pass; 30 focused tests pass.
The full exclusion-only CPU gate passes 4,682 tests plus one expected ASan-only
GLFW skip (4,683 selected, zero failures, 143.67 s). Claude reviewed the fix;
structural/docs checks pass. No sanitizer or Vulkan execution claim.
