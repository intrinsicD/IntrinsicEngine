# GRAPHICS-036C — Wire RenderWorldPool into the engine frame loop

## Goal
- Wire the runtime-owned `RenderWorldPool` (`GRAPHICS-036A`) into `Engine::RunFrame`
  so extraction acquires/publishes a back slot and the renderer consumes/releases
  the front slot across its in-flight window, gated by a default-on
  `RenderConfig::SynchronousExtraction` flag that preserves today's serial
  behavior, and call `MirrorRenderWorldPoolDiagnostics` (`GRAPHICS-036B`) each frame.

## Non-goals
- No flip of `SynchronousExtraction` to pipelined-by-default; that proof is `GRAPHICS-036D`.
- No multi-buffering of the renderer-side snapshot stable storage beyond what the
  pooled lifetime requires for the synchronous default (the pipelined storage proof
  is `GRAPHICS-036D`).
- No change to `RuntimeRenderSnapshotBatch` shape (lifecycle-not-contract, decision 8).
- No threading of extraction/rendering in this slice.

## Context
- Owner layers: `runtime` (composition root owns the pool + drives acquire/publish/
  acquire/release; `GRAPHICS-036` decision 2/3), `graphics/renderer` (consumes only
  `const` snapshot through the existing `SubmitRuntimeSnapshots`/`ExtractRenderWorld`
  seam — no new edge).
- Depends on `GRAPHICS-036A` (pool value type, done) and `GRAPHICS-036B` (diagnostics
  mirror, done).
- `RenderConfig::SynchronousExtraction` (default `true`, `GRAPHICS-036` decision 6)
  lives on the core `RenderConfig` (`src/core/Core.Config.Render.cppm`), never in
  graphics; it sizes the pool (1 logical buffer when synchronous, 3 otherwise).
- Reclamation reuses the `framesInFlight` retire-deadline pattern (decision 4): the
  renderer holds a front reference across the in-flight GPU window and releases it at
  frame retire.

## Required changes
- [ ] Add `RenderConfig::SynchronousExtraction` (default `true`) to the core render config.
- [ ] Have `Engine` own a `RenderWorldPool` sized from the config in `Initialize()`.
- [ ] In `Engine::RunFrame`, `AcquireBack(frameIndex)` before render extraction,
      `PublishFront(slot)` after extraction submit, `AcquireFront(frameIndex)` before
      the renderer consumes, and `ReleaseFront(slot)` at frame retire; call
      `MirrorRenderWorldPoolDiagnostics` into the extraction stats once per frame.
- [ ] Add `contract;runtime` coverage driving a bounded `Engine::Run()` that asserts
      the pool is driven correctly in synchronous mode (behavior-preserving; counters
      report the synchronous baseline).

## Tests
- [ ] `contract;runtime` — bounded `Engine::Run()` in synchronous mode preserves the
      existing single-snapshot behavior; pool lifecycle is exercised; diagnostics
      mirror reports the synchronous baseline.
- [ ] CPU gate: `ctest --test-dir build/ci -L runtime -LE 'gpu|vulkan|slow|flaky-quarantine'` green.

## Docs
- [ ] Document the pool frame-loop wiring + `SynchronousExtraction` default in
      `src/runtime/README.md` (canonical frame-loop phases) and `docs/architecture/graphics.md`.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [ ] `Engine::RunFrame` drives the pool acquire/publish/acquire/release sequence and
      mirrors diagnostics, with the synchronous default preserving current behavior.
- [ ] No new layering violations; default CPU gate stays green.
- [ ] `GRAPHICS-036D` remains the pipelined-path proof follow-up.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci -L runtime -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Flipping `SynchronousExtraction` to pipelined-by-default (that is `GRAPHICS-036D`).
- Mutating snapshot data from graphics; removing the snapshot-extraction boundary.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `Operational` — the pool is wired into the real `Engine::RunFrame` path
  under the reference config and validated on the CPU/null gate (no Vulkan needed).
- The pipelined (render-N-1) proof with multi-buffered storage is owned by `GRAPHICS-036D`.
