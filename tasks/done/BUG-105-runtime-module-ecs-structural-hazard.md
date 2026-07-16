---
id: BUG-105
theme: G
depends_on: []
---
# BUG-105 — Runtime module reader races ECS structural mutation

## Status

- Completed on 2026-07-16; owner: Codex; implementation commit: `f0ea3987`;
  branch: `agent/sandbox-model-workflow-completion`; PR:
  [`#1024`](https://github.com/intrinsicD/IntrinsicEngine/pull/1024).
- Repaired exact-head
  [`pr-fast` run 29531364667](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29531364667)
  completed successfully in 15m31s, including the unit/contract test stage
  that exposed the original sanitizer failure.

## Goal

- Prevent runtime module sim systems from accessing EnTT registry storage
  metadata concurrently with baseline ECS passes that add or remove
  components.

## Non-goals

- No global ECS mutex, serial-only scheduler mode, or new hazard abstraction.
- No change to component-level `Read<T>` / `Write<T>` semantics or named
  signal ordering.
- No weakening, skip, quarantine, or blind rerun of the failing sanitizer
  gate.

## Context

- Symptom: exact-head `pr-fast` run
  [`29526309187`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29526309187)
  failed `RuntimeModule.RegistrationOrderDoesNotChangeHooksOrSchedule` with an
  AddressSanitizer heap-use-after-free.
- Expected behavior: a module sim-system registry reader and an ECS pass that
  changes registry structure must be ordered through the existing
  `FrameGraph.RegistryStructure` hazard token, independent of registration
  order and worker timing.
- Root cause: the module reader declared only `Read<WorldMatrix>`, while
  `RenderSync` declared component writes but not `StructuralWrite()`. T0 read
  EnTT's storage map from `all_of<WorldMatrix>` as worker T5 first-created the
  `DirtyTransform` storage, reallocating the map and freeing the node T0 was
  reading.
- The same baseline bundle also adds/removes transform-update and world-bounds
  components. Runtime owns composition and exposes `ActiveWorld` to every
  module sim-system, so runtime can conservatively declare the existing
  structural-read token; ECS systems own the matching structural-write
  declarations.
- A clean, ccache-disabled focused rebuild succeeded and the timing-sensitive
  local harness passed 100/100 repetitions; the hosted ASan allocation/free
  stacks provide the authoritative repro.
- Right-sizing: reuse `StructuralRead()` / `StructuralWrite()` already exported
  by `Extrinsic.Core.FrameGraph`; do not add a lock, service, registry wrapper,
  or second scheduling policy.

## Required changes

- [x] Give every runtime module sim-system an implicit structural read before
      applying its component-specific setup declarations.
- [x] Mark promoted baseline ECS passes that add/remove components as
      structural writers.
- [x] Preserve the existing named-signal and component hazard declarations.

## Tests

- [x] Add a deterministic contract proving a baseline structural writer and a
      module reader cannot share an execution layer.
- [x] Pass the original engine registration-order harness repeatedly under the
      sanitizer-enabled `ci` preset.
- [x] Pass the focused runtime/ECS structural-hazard selections and the
      default CPU-supported gate.

## Docs

- [x] Document the implicit module structural-read contract and the explicit
      structural-write requirement for systems that add/remove components.
- [x] Synchronize the done-task indexes, retirement log, and session brief
      after the repaired exact-head hosted gate passed.

## Acceptance criteria

- [x] The exact ASan access pattern is ordered by the existing registry
      structure token without a global lock or broad scheduler fallback.
- [x] Deterministic execution-layer coverage fails when either side of the
      structural hazard is removed and passes with the fix.
- [x] A repaired exact-head `pr-fast` workflow passes.
- [x] Layering, task policy, and documentation checks remain green.

## Verification

```bash
cmake --preset ci
CCACHE_DISABLE=1 cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicECSTests
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeModuleSchedule\..*Structural|^RuntimeEcsSystemBundle\..*Structural|^RuntimeModule\.RegistrationOrderDoesNotChangeHooksOrSchedule$|^ECSRenderSync\.' \
  --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -R '^RuntimeModule\.RegistrationOrderDoesNotChangeHooksOrSchedule$' \
  --repeat until-fail:100 --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

Local results on 2026-07-16:

- clean `ci` configure plus ccache-disabled `IntrinsicRuntimeContractTests`
  and `IntrinsicECSTests` builds: passed;
- focused runtime/ECS selection: 9/9 passed;
- `RuntimeModule.RegistrationOrderDoesNotChangeHooksOrSchedule`: 100/100
  sanitizer repetitions passed;
- complete `IntrinsicTests` build: passed;
- default CPU-supported gate: 3791/3791 passed in 700.55 seconds;
- strict layering, task-policy, task-state-link, test-layout, root-hygiene,
  and documentation-link checks: passed.
- repaired exact-head `pr-fast` run `29531364667`: passed in 15m31s.

## Forbidden changes

- Serializing the entire fixed-step graph or disabling worker execution.
- Adding an ECS dependency to `core` or moving runtime composition into ECS.
- Treating component-specific `Read<T>` / `Write<T>` hazards as sufficient for
  storage-map mutation.
- Shipping only a timing-based sleep or a test-local workaround.
