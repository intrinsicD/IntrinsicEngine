# GRAPHICS-036D — Pipelined render-N-1 integration proof

## Goal
- Prove the pipelined sim-N / render-N-1 path against the double-buffered
  `RenderWorldPool` (`GRAPHICS-036A`/`036C`): with `RenderConfig::SynchronousExtraction
  = false`, validate that the renderer consumes snapshot N-1 while extraction writes
  snapshot N, with a deterministic opt-in CPU integration test.

## Non-goals
- No threading model beyond the documented snapshot handoff (the pool's atomics
  already make the index/refcount handoff safe; this slice proves the contract, not a
  new scheduler).
- No change to `RuntimeRenderSnapshotBatch` shape.
- No flip of the production default away from synchronous unless the proof is green
  and the task explicitly records the flip.

## Context
- Owner layers: `runtime` (pool + multi-buffered snapshot stable storage), `graphics/renderer`
  (consumer of the previous-frame snapshot).
- Depends on `GRAPHICS-036C` (engine wiring + `SynchronousExtraction` flag).
- `GRAPHICS-036` decision 6 keeps synchronous the unconditional default until this
  slice proves the pipelined path; decision 8 keeps the snapshot shape stable (only
  its lifetime becomes multi-buffered).
- The proof needs the renderer-side snapshot stable storage to exist in N copies so
  render-N-1 reads a distinct buffer from the one extraction-N writes; that storage
  multi-buffering is the substantive work of this slice.

## Required changes
- [ ] Multi-buffer the renderer-side snapshot stable storage so a pooled front slot
      maps to a distinct retained snapshot copy.
- [ ] Add an opt-in deterministic `integration` test that runs N frames with
      `SynchronousExtraction = false` and asserts render-of-frame-N observes the
      snapshot fields produced at frame N-1.
- [ ] Assert the three pool counters (stall/skip/frame-age) report the expected
      pipelined values (frame age ≥ 1).

## Tests
- [ ] `integration` — deterministic pipelined N-frame run; render-N-1 field assertion;
      frame-age ≥ 1; no torn snapshot.
- [ ] `contract;runtime` — regression that synchronous mode still reports frame age 0.
- [ ] CPU gate stays green.

## Docs
- [ ] Update `src/runtime/README.md` and `docs/architecture/graphics.md` with the
      pipelined-path proof and the production default decision.
- [ ] Update `docs/architecture/rendering-three-pass.md` snapshot-lifetime callout.

## Acceptance criteria
- [ ] The pipelined path is proven by a deterministic opt-in integration test.
- [ ] Synchronous mode remains correct and is regression-tested.
- [ ] Any production-default flip is explicitly recorded.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Removing the snapshot-extraction boundary or mutating snapshot data from graphics.
- Introducing live ECS access in graphics.
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `Operational` — the pipelined path is exercised by a deterministic opt-in
  integration test on the CPU/null gate; no `gpu;vulkan` follow-up is owed for this
  CPU-deterministic proof.
