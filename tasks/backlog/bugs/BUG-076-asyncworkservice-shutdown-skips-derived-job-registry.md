---
id: BUG-076
theme: G
depends_on: []
---
# BUG-076 — AsyncWorkService::ShutdownAndDrain does not drain the derived job registry

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
- [ ] Either drain/quiesce `m_DerivedJobRegistry` in `ShutdownAndDrain`, or add a
      comment/assertion documenting that the registry has no independent lifetime
      beyond the executor and why the executor drain fully covers it.

## Tests
- [ ] Confirm no derived job callback can run after `ShutdownAndDrain` returns
      (contract test or asserted invariant).
- [ ] Default CPU gate.

## Docs
- [ ] Note the shutdown/drain ordering contract in the async-work README.

## Acceptance criteria
- [ ] Shutdown leaves no derived background work able to run.
- [ ] Default CPU gate and strict structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'AsyncWork|Streaming' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
