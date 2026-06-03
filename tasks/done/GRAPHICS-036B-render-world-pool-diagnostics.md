# GRAPHICS-036B — RenderWorldPool diagnostics on extraction stats

- Status: completed (2026-06-03; `CPUContracted`).
- Owner / agent: rendering modernization multi-task loop (`claude/graphics-multi-task-loop-fk4WV`)
- Branch: `claude/graphics-multi-task-loop-fk4WV`
- Commit reference: this task-landing commit.
- Next verification step: none; retired. Pool wiring (`Operational`) owned by `GRAPHICS-036C`.

## Goal
Land the second implementation child of the retired `GRAPHICS-036` planning slice
(`GRAPHICS-036-Impl-B`): surface the `RenderWorldPool` (`GRAPHICS-036A`) diagnostics
read-only on the runtime extraction diagnostics surface per `GRAPHICS-036` decision 7,
via a pure mirror function with `contract;runtime` coverage.

## Non-goals
- No engine/extraction wiring of the pool into the frame loop (`GRAPHICS-036C`).
- No `RenderConfig::SynchronousExtraction` flag (`GRAPHICS-036C`/`GRAPHICS-036D`).
- No bucketed-frame-age histogram (decision 7 mentions it as optional; the scalar
  `RenderWorldFrameAgeFrames` is the surfaced form and no histogram follow-up is owed).
- No change to `RuntimeRenderSnapshotBatch` shape or any pass.

## Context
- Owner layer: `runtime`. The three counters live on `RuntimeRenderExtractionStats`
  (`Extrinsic.Runtime.RenderExtraction`); the authoritative atomic counters remain on
  the pool. `MirrorRenderWorldPoolDiagnostics(const RenderWorldPool&,
  RuntimeRenderExtractionStats&)` is a pure copy so the caller controls cadence.
- The fields stay zero until `GRAPHICS-036C` wires the pool into the frame loop and
  calls the mirror; the mirror + fields are validated in isolation here.
- Honors `GRAPHICS-036` decision 7 (three counters on the runtime extraction
  diagnostics surface: `RenderWorldPipelineStallCount`,
  `RenderWorldExtractionSkipCount`, `RenderWorldFrameAgeFrames`).

## Required changes
- [x] Add the three `RenderWorld*` counters to `RuntimeRenderExtractionStats` in
      `Runtime.RenderExtraction.cppm`.
- [x] Declare + define `MirrorRenderWorldPoolDiagnostics(...)` (import
      `Extrinsic.Runtime.RenderWorldPool` in the interface + implementation units).
- [x] Extend `tests/contract/runtime/Test.RenderWorldPool.cpp` with mirror cases.

## Tests
- [x] `contract;runtime` — zero mirror for an untouched pool; stall+skip+frame-age
      mirror matches the pool diagnostics after a driven schedule.
- [x] CPU gate: `ctest --test-dir build/ci -R RenderWorldPool` (12/12).

## Docs
- [x] Update the `Extrinsic.Runtime.RenderWorldPool` README row to describe the mirror.

## Acceptance criteria
- [x] The mirror compiles and the new `contract;runtime` cases pass.
- [x] No new layering violations; module inventory regenerated.
- [x] `GRAPHICS-036C/D` remain unopened.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci -R RenderWorldPool --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Wiring the pool into the frame loop (that is `GRAPHICS-036C`).

## Maturity
- Target: `CPUContracted` for the diagnostics-mirror contract.
- `Operational` owned by `GRAPHICS-036C` (the mirror is only called once the pool
  is wired into the frame loop). This slice closes `Scaffolded → CPUContracted`.
