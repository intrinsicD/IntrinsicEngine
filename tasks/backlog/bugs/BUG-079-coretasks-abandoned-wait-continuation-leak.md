---
id: BUG-079
theme: G
depends_on: []
---
# BUG-079 — CoreTasks abandoned wait continuation leaks coroutine frame

## Goal
- Give parked task continuations deterministic, exactly-once reclamation when
  their wait token is released or the scheduler shuts down before they resume.

## Non-goals
- No change to the normal rearm/completion path fixed by `BUG-078`.
- No scheduler priority, queue, or wait-mutex sharding work from `CORE-007`.
- No runtime, graphics, or application dependency in `core`.

## Context
- Owner/layer: `core`, specifically `Core.Tasks.WaitToken.cpp`, scheduler
  shutdown, and the `Job` coroutine lifetime contract.
- Review of `BUG-078` found that `Scheduler::ReleaseWaitToken()` clears parked
  raw coroutine handles without resuming or destroying their frames. Scheduler
  teardown similarly discards any remaining parked continuations with the
  scheduler context.
- The default sanitizer configuration disables leak detection, so existing
  ASan UAF coverage does not detect this abandoned-frame path.

## Required changes
- [ ] Define whether wait-token release and scheduler shutdown cancel or drain
      parked task continuations.
- [ ] Transfer enough ownership metadata into parked continuations to reclaim
      each abandoned frame exactly once without resuming against a destroyed
      wait owner.
- [ ] Preserve the single-use resumption-token contract and the `BUG-078`
      no-post-`resume()` handle-access guarantee.

## Tests
- [ ] Add a deterministic core regression with a frame-owned destructor
      sentinel that parks a task, releases the wait token, and observes exactly
      one frame destruction without resumption.
- [ ] Add equivalent scheduler-shutdown coverage for a parked task.
- [ ] Keep the `CoreTasks` rearm and stale-token regressions green under ASan.

## Docs
- [ ] Update `src/core/README.md` with the chosen parked-continuation
      cancellation/reclamation contract.

## Acceptance criteria
- [ ] Releasing a wait token cannot leak or resume its parked task frames.
- [ ] Scheduler shutdown reclaims every parked task frame exactly once.
- [ ] No worker inspects or destroys a coroutine frame after another path has
      reclaimed it.
- [ ] The default CPU-supported gate passes without sanitizer findings.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicCoreWrapperUnitTests
ctest --test-dir build/ci --output-on-failure -R '^CoreTasks\.' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not resume a coroutine merely to make its frame destructible after its
  wait owner is gone.
- Do not add a leak-sanitizer exemption or quarantine the regression.
- Do not introduce higher-layer dependencies into `core`.
