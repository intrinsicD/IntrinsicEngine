---
id: RUNTIME-141
theme: F
depends_on: []
---
# RUNTIME-141 — Async editor method-command lane (no heavy compute in the ImGui callback)

## Goal
- Editor-triggered method runs (CPU K-Means, Progressive Poisson, denoise,
  remesh, simplify, registration) execute asynchronously through the
  existing `StreamingExecutor`/`DerivedJobRegistry` machinery with
  generation-keyed main-thread applies — never synchronously inside
  `ImGuiAdapter::EndFrame` within `Engine::RunFrame()`.

## Non-goals
- No changes to method algorithms, outputs, or parameters.
- The async GPU readback helper is owned by `RUNTIME-137`; Progressive
  Poisson GPU parity and its `ReadBuffer`/`vkDeviceWaitIdle` drain are owned
  by `METHOD-014` (+ `RUNTIME-137` adoption). This task owns the *CPU
  command lane* only.
- Selected-entity model/inspector derivations are owned by `RUNTIME-138`;
  this task owns explicit method-run commands.
- No new scheduler (reuse `StreamingExecutor`/`DerivedJobRegistry`).

## Context
- Owner/layer: `runtime` (`Runtime.SandboxEditorUi` command handlers,
  `Runtime.StreamingExecutor`, `Runtime.DerivedJobGraph`).
- Today editor commands run method compute inline inside the per-frame ImGui
  editor callback: `RunKMeansForSandbox`
  (`src/runtime/Editor/Runtime.SandboxEditorUi.cpp:4286-4308`),
  `RunProgressivePoissonAndPublish` → synchronous `PPR::Compute`
  (`:4789-4804`), and the denoise/remesh/simplify handlers (`:16260, 16367`).
  A long solve freezes the frame for its full duration. The K-Means *GPU*
  queue and `AsyncBufferReadback` already demonstrate the intended
  poll-based pattern.
- The ECS registry is single-threaded: workers must consume
  generation-stamped immutable snapshots captured on the main thread; stale
  results are discarded on apply (same model as `RUNTIME-138`).
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R10.

## Control surfaces
- UI: method panels show pending/running/ready/stale/failed job state and
  keep a cancel/re-run affordance; results apply when ready.
- Agent/CLI: existing method command surfaces keep working; completion is
  observable through the same job state.
- Config: none new.

## Required changes
- [ ] Introduce a shared "editor method job" submission helper over
      `StreamingExecutor`/`DerivedJobRegistry`: snapshot inputs on the main
      thread, run compute on the worker lane, apply results on the main
      thread with generation checks and bounded per-frame apply budget.
- [ ] Convert the CPU K-Means editor command to the helper (parity with the
      existing GPU-queue UX: same published label/color properties).
- [ ] Convert Progressive Poisson CPU runs to the helper.
- [ ] Convert denoise/remesh/simplify (and registration alignment) commands
      to the helper.
- [ ] Panels reflect job state instead of blocking; a second submit while
      one runs either queues or replaces per current UX expectations
      (document choice per panel).

## Tests
- [ ] Contract: a submitted method job runs off the main thread, applies its
      result on a later frame, and the applied output equals the previous
      synchronous output for a fixed seed/scene (per converted command).
- [ ] Contract: a stale job (scene/geometry generation changed mid-run) is
      discarded without mutating state.
- [ ] Contract: the ImGui editor callback duration stays bounded while a
      heavy job runs (timing probe with a deliberately slow job).
- [ ] Existing method/editor command suites stay green.

## Docs
- [ ] Update `src/runtime/README.md` editor-command execution model.

## Acceptance criteria
- [ ] No editor method command executes its solve inside the ImGui callback
      (verified per converted command by the timing contract).
- [ ] Method outputs unchanged for deterministic fixtures.
- [ ] Default CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding a second ad hoc scheduler next to `StreamingExecutor`.
- Worker-thread access to the live ECS registry or renderer state.
- Changing method numerical behavior while moving execution.

## Maturity
- Target: `Operational` (the sandbox editor path is the subject); CPU gate
  contracts prove the job lifecycle, a sandbox run proves responsiveness.
