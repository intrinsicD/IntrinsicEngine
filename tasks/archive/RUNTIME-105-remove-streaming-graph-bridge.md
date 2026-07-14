---
id: RUNTIME-105
theme: F
depends_on: []
---
# RUNTIME-105 — Remove the deprecated GetStreamingGraph() TaskGraph bridge

## Status

- State: `done` — retiring to `tasks/done/` on 2026-06-15 at maturity
  `Retired`.
- Owner: agent. Branch: `main`.
- PR/commit: this retirement commit.
- Resolution: `Engine::GetStreamingGraph()`, `m_StreamingGraph`, and the
  per-frame `SubmitStreamingGraphToExecutor` compatibility conversion are
  deleted. `Engine` now owns only the persistent `StreamingExecutor` path for
  runtime async asset IO / geometry processing work. The stale
  `RuntimeEngineLayering` harness was corrected to inspect
  `Core.FrameLoop.cpp`, where the promoted frame-loop implementation lives.
- Verification: see the commands listed below.

## Goal

- Delete the temporary `Engine::GetStreamingGraph()` TaskGraph compatibility
  bridge (accessor, `m_StreamingGraph` member, and the
  `SubmitStreamingGraphToExecutor` frame conversion) now that
  `Extrinsic.Runtime.StreamingExecutor` is the primary persistent async
  streaming path.

## Non-goals

- No `StreamingExecutor` behavior or API changes.
- No new streaming features; this is a pure bridge removal.
- No `src/legacy/` edits (legacy callers retire with their subtrees under
  `LEGACY-001..010`).

## Value gate

- Not applicable: this is a pure dead-code bridge removal. The promoted tree
  already has zero callers, so there is no workflow comparison to run; the
  only value question is keeping the §13 temporary-exception ledger honest.

## Context

- Owner/layer: `runtime` (`src/runtime/Runtime.Engine.cppm` /
  `Runtime.Engine.cpp`, `src/runtime/README.md`).
- Opened by `HARDEN-078` to give the `AGENTS.md` §13 temporary bridge a
  tracked removal owner; the deprecation message and the runtime README's
  "Streaming integration" note both reference this task.
- As of 2026-06-10 the promoted tree has **zero** `GetStreamingGraph()`
  consumers outside the Engine bridge itself (`git grep -n
  "GetStreamingGraph" -- src tests ':!src/legacy'` returns only the
  accessor declaration/definition and the bridge comment), so the
  per-frame `SubmitStreamingGraphToExecutor` call always observes an empty
  graph and early-outs.

## Required changes

- [x] Confirm the consumer grep is still empty outside `src/legacy/`; if a
      promoted consumer appeared, migrate it to `StreamingExecutor` first.
- [x] Remove the `GetStreamingGraph()` accessor (declaration + definition),
      the `m_StreamingGraph` member, and `SubmitStreamingGraphToExecutor`
      plus its RunFrame call site.
- [x] Update the `src/runtime/README.md` "Streaming integration" note to
      state that `StreamingExecutor` is the only streaming path.

## Tests

- [x] Default CPU gate stays green:
      `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`.
- [x] No new behavior tests; the bridge is dead code on the promoted path.
      The stale `RuntimeEngineLayering` source-inspection harness was corrected
      so the existing runtime layering prefix can cover this deletion.

## Docs

- [x] `src/runtime/README.md` streaming note updated.
- [x] Regenerate `docs/api/generated/module_inventory.md` (public Engine
      surface shrinks).

## Acceptance criteria

- [x] `git grep -n "GetStreamingGraph\|SubmitStreamingGraphToExecutor" -- src tests ':!src/legacy'`
      returns nothing.
- [x] Default CPU gate green.

## Verification

```bash
git grep -n "GetStreamingGraph\|SubmitStreamingGraphToExecutor" -- src tests ':!src/legacy'
# Passed: no output.

python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
# Passed: wrote docs/api/generated/module_inventory.md (484 modules).

cmake --build --preset ci --target IntrinsicTests
# Passed.

cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests IntrinsicRuntimeGraphicsCpuTests
# Passed.

ctest --test-dir build/ci --output-on-failure -R RuntimeFrameLoopContract --timeout 60
# Passed: 18/18 tests.

ctest --test-dir build/ci --output-on-failure -R RuntimeStreamingExecutor --timeout 60
# Passed: 15/15 tests.

ctest --test-dir build/ci --output-on-failure -R RuntimeEngineLayering --timeout 60
# Passed: 9/9 tests.

ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Passed: 2972/2972 tests.

python3 tools/repo/check_layering.py --root src --strict
# Passed: no layering violations found.
```

## Forbidden changes

- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Changing `StreamingExecutor` semantics.

## Maturity

- Target: `Retired` (bridge deletion; no maturity progression on a new seam,
  and no `Operational` follow-up is owed).
