---
id: RUNTIME-141
theme: F
depends_on: []
---
# RUNTIME-141 — Async editor method-command lane (no heavy compute in the ImGui callback)

## Goal
- Editor-triggered heavy action buttons and method runs (CPU K-Means,
  Progressive Poisson, denoise, remesh, simplify, registration, and similar
  geometry/method commands) enqueue runtime-owned async jobs/tasks through the
  existing `StreamingExecutor`/`DerivedJobRegistry` machinery with
  generation-keyed main-thread applies, then return to the frame loop without
  stalling rendering.

## Non-goals
- No changes to method algorithms, outputs, or parameters.
- The async GPU readback helper is owned by `RUNTIME-137`; Progressive
  Poisson GPU parity and its `ReadBuffer`/`vkDeviceWaitIdle` drain are owned
  by `METHOD-014` (+ `RUNTIME-137` adoption). This task owns the *CPU
  command lane* only.
- Selected-entity model/inspector derivations are owned by `RUNTIME-138`;
  this task owns explicit method-run commands.
- No new scheduler (reuse `StreamingExecutor`/`DerivedJobRegistry`).
- Lightweight UI state changes, parameter edits, selection/gizmo commands, and
  other frame-critical cheap commands may remain immediate when they are proven
  not to run heavy compute or blocking IO.

## Context
- Owner/layer: `runtime` (`Runtime.SandboxEditorUi` command handlers,
  `Runtime.StreamingExecutor`, `Runtime.DerivedJobGraph`).
- User-reported bug (2026-07-05): UI buttons that trigger expensive work should
  create async tasks/jobs and must not stall or block rendering while the work is
  running.
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
- [ ] Inventory Sandbox editor action buttons and classify each heavyweight
      command as already queued, lightweight/immediate, or still synchronously
      expensive.
- [ ] Introduce a shared "editor method/action job" submission helper over
      `StreamingExecutor`/`DerivedJobRegistry`: snapshot inputs on the main
      thread, run compute on the worker lane, apply results on the main
      thread with generation checks and bounded per-frame apply budget.
- [ ] Make converted button handlers return a queued/pending status and job id
      or diagnostics immediately, without executing the heavy body inline.
- [ ] Convert the CPU K-Means editor command to the helper (parity with the
      existing GPU-queue UX: same published label/color properties).
- [ ] Convert Progressive Poisson CPU runs to the helper.
- [ ] Convert denoise/remesh/simplify (and registration alignment) commands
      to the helper.
- [ ] Panels reflect job state instead of blocking; a second submit while
      one runs either queues or replaces per current UX expectations
      (document choice per panel).

## Tests
- [ ] Contract: a representative converted UI button creates a pending job and
      returns before the heavy compute body runs.
- [ ] Contract: a submitted method job runs off the main thread, applies its
      result on a later frame, and the applied output equals the previous
      synchronous output for a fixed seed/scene (per converted command).
- [ ] Contract: a stale job (scene/geometry generation changed mid-run) is
      discarded without mutating state.
- [ ] Contract: the ImGui editor callback duration stays bounded while a
      heavy job runs (timing probe with a deliberately slow job).
- [ ] Contract: render extraction/prepare can advance while an editor method
      job is pending.
- [ ] Existing method/editor command suites stay green.

## Docs
- [ ] Update `src/runtime/README.md` editor-command execution model.

## Acceptance criteria
- [ ] Heavy editor UI buttons create runtime jobs/tasks instead of executing
      solves or blocking IO inside the ImGui callback.
- [ ] No editor method command executes its solve inside the ImGui callback
      (verified per converted command by the queued-status and timing
      contracts).
- [ ] Rendering remains advanceable while converted jobs are pending; only the
      bounded main-thread apply phase may mutate committed state.
- [ ] Method outputs unchanged for deterministic fixtures.
- [ ] Default CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi|EditorMethodJob|DerivedJob|StreamingExecutor' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding a second ad hoc scheduler next to `StreamingExecutor`.
- Worker-thread access to the live ECS registry or renderer state.
- Changing method numerical behavior while moving execution.
- Running heavy geometry/method/IO work directly from ImGui button handlers.
- Letting renderer code own editor job scheduling or editor completion state.

## Maturity
- Target: `Operational` (the sandbox editor path is the subject); CPU gate
  contracts prove the job lifecycle, a sandbox run proves responsiveness.
- This is the canonical task for the 2026-07-05 user-reported bug where UI
  buttons stall/block rendering instead of creating async jobs; no duplicate
  `BUG-*` task is filed unless a narrower repro remains after this task lands.
