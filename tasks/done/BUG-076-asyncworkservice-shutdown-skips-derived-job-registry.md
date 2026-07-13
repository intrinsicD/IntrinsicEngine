---
id: BUG-076
theme: G
depends_on: []
---
# BUG-076 — AsyncWorkService::ShutdownAndDrain does not drain the derived job registry

## Status
- Completed 2026-07-13 at `CPUContracted`.
- Commit: production fix `6d6f784a`; this task-retirement commit records the
  final aggregate verification and synchronized task history.

## Goal
- Quiesce the derived job registry at shutdown, not only the streaming executor,
  so no derived background work can outlive `ShutdownAndDrain`.

## Non-goals
- No change to the frame-time async work scheduling behavior.

## Context
- Owner/layer: `runtime`; `src/runtime/Runtime.AsyncWorkService.cpp`.
- `AsyncWorkService::ShutdownAndDrain` drains only `m_StreamingExecutor`
  (`Runtime.AsyncWorkService.cpp:21-27`), while `DrainCompletions`,
  `ApplyMainThreadResults`, and `PumpBackground` all prefer
  `m_DerivedJobRegistry`. The registry is never explicitly told to quiesce/drain
  at shutdown.
- Likely benign today because the registry runs its background work on the same
  executor threads, but the asymmetry is fragile: a future registry that owns
  independent workers or in-flight results would not be drained. Verify and make
  the shutdown symmetric or document why the executor drain is sufficient.

## Required changes
- [x] Either drain/quiesce `m_DerivedJobRegistry` in `ShutdownAndDrain`, or add a
      comment/assertion documenting that the registry has no independent lifetime
      beyond the executor and why the executor drain fully covers it.

## Tests
- [x] Confirm no derived job callback can run after `ShutdownAndDrain` returns
      (contract test or asserted invariant).
- [x] Default CPU gate.

## Docs
- [x] Note the shutdown/drain ordering contract in the async-work README.

## Acceptance criteria
- [x] Shutdown leaves no derived background work able to run.
- [x] Default CPU gate and strict structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'AsyncWork|Streaming' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

Closure verification on 2026-07-13:

- The initial executor-drain plus registry drain/apply implementation exposed a
  real remaining hole: `ShutdownCancelsUnreadiedDerivedReadback` failed
  deterministically with the record still `Applying`; after readiness changed,
  one result applied and its callback ran after shutdown. The final fix snapshots
  and cancels every non-terminal survivor after the last drain/apply pass.
- `RuntimeAsyncWorkService.ShutdownDrainsDerivedReadbackBeforeReturn` and
  `RuntimeAsyncWorkService.ShutdownCancelsUnreadiedDerivedReadback` each passed
  100 repetitions under the `ci` preset's ASan/UBSan configuration. The broader
  `AsyncWorkService|DerivedJobGraph` selection passed 9/9.
- `IntrinsicTests` built successfully and the complete default CPU-supported
  gate passed 3684/3684. Strict layering, task-policy, test-layout, and
  documentation-link checks pass.

## Completion

- Completed: 2026-07-13. Maturity: `CPUContracted`.
- Outcome: shutdown first joins executor work, drains derived completions and
  ready readbacks, applies newly ready results, then cancels every survivor so
  an unreadied callback cannot cross the shutdown boundary.
- This is a deterministic CPU lifecycle contract; no backend-operational
  follow-up is owed.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Closed at `CPUContracted`; sanitizer-backed contracts cover both a ready
  readback and an unreadied survivor, and no hardware backend is involved.
