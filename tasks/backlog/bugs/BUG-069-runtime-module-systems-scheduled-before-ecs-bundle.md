---
id: BUG-069
theme: G
depends_on: []
---
# BUG-069 — RuntimeModule sim-systems scheduled before the baseline ECS bundle

## Goal
- Guarantee that module-registered sim-systems which consume baseline ECS
  outputs (`Transform::WorldMatrix`, the `TransformUpdate` signal) are scheduled
  **after** the promoted baseline bundle, independent of call-site order.

## Non-goals
- No change to the core `FrameGraph` sequential RAW/WAR/WAW hazard model.
- No change to `FinalizeForBoot`'s module-vs-module canonical ordering (that
  guarantee is intact and tested).

## Context
- Owner/layer: `runtime`; `src/runtime/Runtime.Engine.FrameLoop.cppm`,
  `src/runtime/Runtime.ModuleSchedule.*`.
- Introduced by merge `76528e6`. Pre-merge main (with the `BUG-066` fix)
  registered the promoted ECS bundle **first** then module systems
  (`c476ea6:src/runtime/Runtime.Engine.cpp:676` then `:684`). The merged tree
  reverses it: `RunFixedStepSimulationTicks` calls `registerModuleSystems`
  (`Runtime.Engine.FrameLoop.cppm:378`) **before** `RegisterPromotedEcsSystemBundle`
  (`:383`), per the RUNTIME-091 "bundle last" comment.
- The core `FrameGraph` orients every resource hazard edge (RAW/WAW/WAR) from the
  earlier-inserted pass to the later-inserted pass
  (`src/core/Core.Dag.TaskGraph.cpp:274-321`); the topological sort therefore
  preserves insertion order for any two passes that touch the same resource. It
  only reorders/parallelizes mutually independent passes.
- Consequence with "bundle last": a module pass declaring `Read<WorldMatrix>`
  gets a WAR edge module→bundle and runs before the bundle writes it → reads the
  previous substep's matrix (silent). A module pass doing `WaitFor("TransformUpdate")`
  (the baseline signal, `src/ecs/Systems/ECS.System.TransformHierarchy.cpp:88`)
  finds no prior signaler → `Compile()` returns `InvalidState`
  (`Core.Dag.TaskGraph.cpp:328-339`) → the whole substep's FrameGraph fails and
  every sim pass that substep is dropped (logged at
  `Runtime.Engine.FrameLoop.cppm:397`).
- This is latent until a runtime module actually consumes a baseline output, but
  it directly reverses `BUG-066`'s shipped ordering and its documented rule.

## Slice plan
- **Slice A (correctness, this fix).** Restore "bundle-first": register
  `RegisterPromotedEcsSystemBundle` before `registerModuleSystems` in
  `RunFixedStepSimulationTicks`, matching main/`BUG-066`. Add a regression test.
  Reconcile the RUNTIME-091 "bundle last" comment vs the `BUG-066` "bundle first"
  rule so the code and docs agree.
- **Slice B (durable, follow-up owned by `BUG-072`).** Bring the baseline bundle
  into the same named-signal schedule as modules (bundle is the root signaler),
  and translate the declarative `WaitForSignals`/`SignalLabels` fields into real
  per-tick `builder.WaitFor`/`Signal` edges so ordering no longer depends on
  call-site or insertion order at all.

## Required changes
- [ ] Move `RegisterPromotedEcsSystemBundle(frameGraph, scene)` to run before
      `registerModuleSystems(...)` in `RunFixedStepSimulationTicks`
      (`Runtime.Engine.FrameLoop.cppm`).
- [ ] Reconcile the conflicting ordering rationale: update the RUNTIME-091
      comment and any doc that states "bundle last" to reflect the `BUG-066`
      "bundle-first / baseline-produces, modules-consume" rule.

## Tests
- [ ] Add a contract test: a runtime module that `Read<Transform::WorldMatrix>`
      (or `WaitFor("TransformUpdate")`) is scheduled after the baseline bundle
      and observes the current substep's world matrix (not the previous one),
      and its substep compiles.
- [ ] `ctest --test-dir build/ci --output-on-failure -R 'RuntimeModule|EcsSystemBundle' --timeout 60`.
- [ ] Default CPU gate.

## Docs
- [ ] Update `docs/architecture/runtime.md` and the feature-module playbook to
      state that the baseline bundle runs before module sim-systems and is the
      source of the baseline `TransformUpdate` signal modules may wait on.

## Acceptance criteria
- [ ] A module consuming a baseline ECS output schedules after the baseline
      bundle and reads current-substep data.
- [ ] A module `WaitFor("TransformUpdate")` compiles (finds a prior signaler).
- [ ] Code comments and architecture docs agree on the bundle/module order.
- [ ] Default CPU gate and strict structural checks pass.
- [ ] Durable signal-unification is owned by `BUG-072`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeModule|EcsSystemBundle' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not change core `FrameGraph` hazard orientation.
- Do not make pass names imply producer/consumer direction.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` for Slice A; durable signal unification (`Operational`
  robustness) owned by `BUG-072`.
