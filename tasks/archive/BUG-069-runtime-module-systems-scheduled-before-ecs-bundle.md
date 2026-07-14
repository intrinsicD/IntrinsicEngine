---
id: BUG-069
theme: G
depends_on: []
---
# BUG-069 — RuntimeModule sim-systems scheduled before the baseline ECS bundle

## Status
- Completed 2026-07-10 at `CPUContracted` on branch `main` in this workspace.
- Implementation commits: `3102e60f`, `c3794716`, `aead3bb0`, and
  `f45371c6`; the retirement commit adds the missing real-engine regression and
  synchronizes task state.
- Commit: implementation commits above; retirement closure is this local task
  commit.
- Durable call-site-independent signal unification remains owned by `BUG-072`.

## Goal
- Guarantee that module-registered sim-systems which consume baseline ECS
  outputs (`Transform::WorldMatrix`, the `TransformUpdate` signal) are scheduled
  **after** the promoted baseline bundle by composing the bundle first each
  fixed substep, independent of module add/registration order.

## Non-goals
- No change to the core `FrameGraph` sequential RAW/WAR/WAW hazard model.
- No change to `FinalizeForBoot`'s module-vs-module canonical ordering (that
  guarantee is intact and tested).

## Context
- Owner/layer: `runtime`; `src/runtime/Runtime.Engine.FrameLoop.cppm`,
  `src/runtime/Runtime.ModuleSchedule.*`.
- Introduced by merge `76528e6`. Pre-merge main (with the `BUG-066` fix)
  registered the promoted ECS bundle **first** then module systems
  (`c476ea6:src/runtime/Runtime.Engine.cpp:676` then `:684`). At discovery, the
  merged tree reversed it: `RunFixedStepSimulationTicks` called
  `registerModuleSystems` (`Runtime.Engine.FrameLoop.cppm:378`) **before**
  `RegisterPromotedEcsSystemBundle` (`:383`), per the RUNTIME-091 "bundle last"
  comment.
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
- [x] Move `RegisterPromotedEcsSystemBundle(frameGraph, scene)` to run before
      `registerModuleSystems(...)` in `RunFixedStepSimulationTicks`
      (`Runtime.Engine.FrameLoop.cppm`).
- [x] Reconcile the conflicting ordering rationale: update the RUNTIME-091
      comment and any doc that states "bundle last" to reflect the `BUG-066`
      "bundle-first / baseline-produces, modules-consume" rule.

## Tests
- [x] Add a contract test: a runtime module that `Read<Transform::WorldMatrix>`
      (or `WaitFor("TransformUpdate")`) is scheduled after the baseline bundle
      and observes the current substep's world matrix (not the previous one),
      and its substep compiles.
- [x] `ctest --test-dir build/ci --output-on-failure -R 'RuntimeModule|EcsSystemBundle' --timeout 60`.
- [x] Default CPU gate.

## Docs
- [x] Update `docs/architecture/runtime.md` and the feature-module playbook to
      state that the baseline bundle runs before module sim-systems and is the
      source of the baseline `TransformUpdate` signal modules may wait on.

## Acceptance criteria
- [x] A module consuming a baseline ECS output schedules after the baseline
      bundle and reads current-substep data.
- [x] A module `WaitFor("TransformUpdate")` compiles (finds a prior signaler).
- [x] Code comments and architecture docs agree on the bundle/module order.
- [x] Default CPU gate and strict structural checks pass.
- [x] Durable signal-unification is owned by `BUG-072`.

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

Closure verification on 2026-07-10:

- `IntrinsicRuntimeContractTests` built with the Clang 23 `ci` preset.
- `RuntimeModule.BaselineTransformConsumerObservesCurrentSubstepWorldMatrix`
  passed through the real `Engine::RunFixedStepSimulationTicks` path. The test
  mutates a transform in `OnSimTick`, declares both a `TransformUpdate` wait and
  a `Read<Transform::WorldMatrix>` hazard, and proves the module sees the
  current substep's translation rather than the prior matrix; the focused
  binary also passed 20 consecutive repetitions.
- The focused `RuntimeModule|EcsSystemBundle` CTest selector passed 18/18.
- The default CPU-supported gate passed 3,656/3,656 in 60.93 s. An earlier run
  in which an unrelated CUDA extension build restarted mid-gate was discarded
  after starving only the benchmark smoke past its 60-second timeout.
- Strict layering, docs synchronization, task policy, task-state links, test
  layout, documentation links, PR-contract mapping, session-brief freshness,
  and diff-whitespace checks passed.

## Completion

- Completed: 2026-07-10. Maturity: `CPUContracted`.
- Outcome: baseline ECS producers are registered before module consumers, the
  external baseline signal is accepted at boot and materialized per tick, and
  the real-engine regression pins current-substep `WorldMatrix` visibility.
- Follow-up: `BUG-072` owns the remaining durable signal-unification audit and
  explicit parallel-pass regression.

## Forbidden changes
- Do not change core `FrameGraph` hazard orientation.
- Do not make pass names imply producer/consumer direction.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `CPUContracted` for Slice A; durable signal unification (`Operational`
  robustness) owned by `BUG-072`.
