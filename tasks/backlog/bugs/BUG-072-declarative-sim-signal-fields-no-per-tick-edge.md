---
id: BUG-072
theme: G
depends_on:
  - BUG-069
---
# BUG-072 — Declarative sim-system signal fields create no per-tick FrameGraph edge

## Goal
- Make `SimSystemDesc::WaitForSignals`/`SignalLabels` produce real per-tick
  FrameGraph ordering edges, so module ordering no longer depends on boot-time
  insertion order surviving into execution. This is the durable fix that
  `BUG-069` Slice B defers to.

## Non-goals
- No change to the core `FrameGraph` hazard model.
- No removal of the boot-time canonical sort in `FinalizeForBoot`.

## Context
- Owner/layer: `runtime`; `src/runtime/Runtime.ModuleSchedule.*`,
  `src/runtime/Runtime.Module.cppm`.
- `RegisterSimSystemsForTick` applies only `record.Desc.Setup` to the builder
  (`Runtime.ModuleSchedule.cpp:194-198`); it never translates the declarative
  `WaitForSignals`/`SignalLabels` fields (`Runtime.Module.cppm:77-78`) into
  `builder.WaitFor`/`builder.Signal` calls.
- Consequence: the declarative fields are load-bearing **only** for
  `FinalizeForBoot`'s boot-time insertion order. Per-tick ordering among module
  systems then rests entirely on insertion order surviving into execution, which
  holds only because module descs default to `MainThreadOnly=true,
  AllowParallel=false` (`Runtime.Module.cppm:72-76`). It breaks if a module sets
  `AllowParallel=true`.
- The contract tests mask this by redundantly hand-calling `b.Signal`/`b.WaitFor`
  inside their `Setup` lambdas (`Test.RuntimeModule.cpp:164-167, 263-266`), so the
  declarative fields are never exercised in isolation. The result is a misleading,
  duplicated API: the same edge must be declared twice.
- With this in place, `BUG-069`'s baseline bundle can be ordered by signal rather
  than call-site position, closing the positional fragility for good.

## Required changes
- [ ] In `RegisterSimSystemsForTick`, translate each system's `WaitForSignals`
      into `builder.WaitFor(...)` and each `SignalLabels` entry into
      `builder.Signal(...)` around the system's `Setup`, so the per-tick graph
      carries the declared edges.
- [ ] Route the promoted baseline bundle's ordering signal (`TransformUpdate`)
      through the same mechanism so a module can express "after baseline" purely
      declaratively (coordinate with `BUG-069`).
- [ ] Remove the now-redundant hand-rolled `b.Signal`/`b.WaitFor` calls from the
      contract-test `Setup` lambdas so the declarative fields are exercised on
      their own.

## Tests
- [ ] A module declaring only `WaitForSignals`/`SignalLabels` (no manual builder
      calls) is correctly ordered per tick, including with `AllowParallel=true`.
- [ ] `ctest --test-dir build/ci --output-on-failure -R RuntimeModule --timeout 60` and default CPU gate.

## Docs
- [ ] Update the feature-module playbook to state that declarative signal fields
      are the supported way to express per-tick order (no manual builder calls
      needed).

## Acceptance criteria
- [ ] Declarative signal fields alone determine per-tick order.
- [ ] Ordering no longer depends on `MainThreadOnly`/insertion order surviving.
- [ ] Default CPU gate and strict structural checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R RuntimeModule --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not change core `FrameGraph` hazard orientation.
- Mixing mechanical file moves with semantic refactors.
