---
id: ARCH-007
theme: F
depends_on: []
maturity_target: CPUContracted
completed: 2026-07-08
---
# ARCH-007 — Kernel command bus with a single pre-sim drain point

## Status

- Retired on 2026-07-08 at `CPUContracted`.
- PR: #1010. Commit: `977244dc` merge.
- The code landed as the
  `Extrinsic.Runtime.CommandBus` module plus Engine pre-sim drain wiring,
  headless runtime contract tests, runtime architecture docs, and regenerated
  module inventory.
- Local authoring could not run the preset build because that environment's
  egress policy blocked the vcpkg-tool download. CI later compiled the full
  PR under `pr-fast` / `ci-linux-clang` / `ci-vulkan`; all nine
  `RuntimeCommandBus.*` contract tests passed. The only remaining CI failures
  were pre-existing `main` defects tracked independently as `BUG-063` and
  `BUG-064`, so ARCH-007's own acceptance evidence is positive.
- `Operational` use of the command bus remains owned by `ARCH-012`, which
  composes a real `ClusteringModule` flow through command → job → event →
  commit.

## Goal
- Give the runtime kernel a `CommandBus`: plain-data command payloads with
  correlation IDs, thread-safe enqueue from any thread/phase, and execution
  main-thread-only at a single drain point in `Engine::RunFrame()` between
  platform input (Phase 1) and fixed-step simulation (Phase 2), per
  [ADR-0024](../../docs/adr/0024-kernel-module-architecture.md) D5/D13.

## Non-goals
- No `CommandSequence` (ADR-0024 D6 defers it to its first real customer).
- No `Pending` outcome status — that needs `JobToken` from `ARCH-009`; this
  task ships `Completed`/`Failed` only.
- No second drain point after the variable tick (rejected in ADR-0024).
- No migration of existing UI/editor code paths onto the bus; consumers
  arrive with `ARCH-011`/`ARCH-012`.
- No undo/redo semantics beyond the post-execution history hook seam;
  `EditorCommandHistory` integration is a follow-up consumer.

## Context
- Owner/layer: `runtime` (`Extrinsic.Runtime.*`; kernel spine per ADR-0024 D9).
- The numbered frame phases in `src/runtime/Runtime.Engine.cpp` (Phase 1–11,
  around lines 3458–3773) contain no command drain today; commands as a
  concept do not exist in `src/` yet.
- ADR-0024 D13: handlers receive a `CommandContext` carrying narrow
  capabilities (active world + handle, bus references, correlation ID), never
  `Engine&`.
- Fail-closed rule: enqueueing a command type with no registered handler must
  surface a loud diagnostic at drain time, not silently drop.

## Implementation Notes

- 2026-07-08: implementation landed on branch
  `claude/research-repo-comparison-vy5x0l` (module, Engine wiring, contract
  tests, docs). Compiler/CTest verification could not run in the authoring
  environment: the session egress policy blocks the vcpkg-tool download
  (`github.com/microsoft/vcpkg-tool/releases/...` → 403), so
  `cmake --preset ci` cannot configure. Structural gates
  (`check_layering.py --strict`, `check_task_policy.py --strict`,
  `check_doc_links.py`, inventory regeneration) ran clean locally; the
  build/test acceptance boxes stay unchecked until the CI presets
  (`pr-fast`/`ci-linux-clang`) verify them. Do not retire this task on the
  authoring session's evidence alone.
- 2026-07-08 (CI round on head `e732e69`, after the BUG-062 budget fix
  unblocked the configure step): the full build compiles under
  `pr-fast`/`ci-linux-clang`/`ci-vulkan` and the suite runs; all nine
  `RuntimeCommandBus.*` contract tests pass (the runtime label suite ran
  860 tests; the only failures anywhere are the pre-existing defects
  tracked as `BUG-063` — streaming-import flakes, red on `main` since
  ≥2026-07-07 — and `BUG-064` — ci-vulkan frame-pacing capture headless
  DISPLAY, red across all branches). ARCH-007's own gate evidence is
  therefore positive; overall PR greenness is blocked only by the
  pre-existing `BUG-063`/`BUG-064` defects.

## Required changes
- [x] New `Extrinsic.Runtime.CommandBus` module (interface `.cppm` +
      implementation `.cpp`): typed handler registration, type-erased MPSC
      enqueue, correlation-ID allocation, drain loop.
- [x] `CommandContext` struct per ADR-0024 D13 (no `Engine&` member).
- [x] `CommandOutcome { Completed | Failed, error string }`; failures and
      missing-handler drains emit a diagnostic (log + counter; event
      integration follows `ARCH-008`).
- [x] Wire the drain call into `Engine::RunFrame()` between Phase 1 and
      Phase 2 with a phase comment matching the existing style.
- [x] Post-execution history hook seam (callback invoked after successful
      execution of a command that declares an inverse payload).
- [x] Built-in `QuitRequested` command replacing direct shutdown calls from
      future module code (ADR-0024 D13).
- [x] PR #1010 review follow-ups: `DiscardPending()` wired into
      `Engine::Shutdown()` so stale commands cannot replay into a
      re-initialized scene (Shutdown()+Initialize() reuse path); an RAII
      guard keeps the drain flag correct on every exit path. The
      reviewer's hook-exception-wedge scenario is impossible in this
      codebase (`-fno-exceptions`: any throw terminates), so no catch
      path exists by design.
- [x] CI round 1 rework: the codebase builds with `-fno-rtti` and
      `-fno-exceptions`; replaced `typeid`/`std::type_index` with the
      FrameGraph's compile-time FNV-1a type tokens
      (`Core::TypeToken<T>()`) plus a `consteval` signature-based
      diagnostics name, and removed all try/catch (a throwing handler
      is a process-terminating defect, not a recoverable outcome).

## Tests
- [x] Contract tests authored (headless, `contract;runtime` labels via
      `RuntimeContractTestObjs`, matching the `Test.RuntimeEcsSystemBundle`
      precedent): drain executes
      in enqueue order on the draining thread; cross-thread enqueue is safe;
      payloads are copied (mutating the source after enqueue does not change
      the drained payload).
- [x] Fail-closed test: enqueue with no handler → drain reports the
      diagnostic and does not crash.
- [x] Handler-failure test: `Failed` outcome carries the error and does not
      abort the drain of subsequent commands.
- [x] Review-regression test: `DiscardPending()` drops queued commands
      without executing them and counts them in `Stats().Discarded`.
      (Throwing-handler/hook tests were removed as invalid — the
      codebase builds with `-fno-exceptions`.)

## Docs
- [x] Regenerate `docs/api/generated/module_inventory.md` (new module).
- [x] Link the drain point from the frame-phase description in
      `docs/architecture/runtime.md` (or the doc that owns the phase list),
      citing ADR-0024.

## Acceptance criteria
- [x] `CommandBus` exists as a kernel module with no domain nouns and no
      imports above `runtime` substrate needs.
- [x] Drain point runs in `Engine::RunFrame()` pre-sim; no other execution
      path exists.
- [x] All listed tests pass under the default CPU gate.
- [x] `Operational` follow-up is owned by `ARCH-012`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Passing `Engine&` through `CommandContext` or any handler surface.
- Exposing an immediate-execution path for commands.
- Extracting or refactoring any existing runtime feature in this task.
- Mixing mechanical file moves with this semantic addition.

## Maturity
- Target achieved: `CPUContracted` (headless contract tests under the default
  CPU gate). `Operational` remains owned by `ARCH-012`.
