# GRAPHICS-036A — Runtime.RenderWorldPool slot-lifecycle value type

- Status: completed (2026-06-03; `CPUContracted`).
- Owner / agent: rendering modernization multi-task loop (`claude/graphics-multi-task-loop-fk4WV`)
- Branch: `claude/graphics-multi-task-loop-fk4WV`
- Commit reference: this task-landing commit.
- Next verification step: none; retired. `Operational` is owned by `GRAPHICS-036C`.

## Goal
Land the first implementation child of the retired `GRAPHICS-036` pipelined-frames
planning slice (named `GRAPHICS-036-Impl-A` there): a runtime-owned
`Extrinsic.Runtime.RenderWorldPool` value type that implements the multi-buffer
slot-lifecycle state machine (atomic front-index publish/acquire, per-slot
refcounts, reclamation queue, and the replace/reuse back-pressure counters) with
`contract;runtime` coverage.

## Non-goals
- No binding of real snapshot payload storage into the pool slots (the
  `RuntimeRenderSnapshotBatch` backing buffers); that is `GRAPHICS-036C`.
- No diagnostics wiring through `RenderDiagnostics` / runtime extraction stats;
  that is `GRAPHICS-036B`.
- No `RenderConfig::SynchronousExtraction` flag plumbing or renderer
  `BeginFrame()`/`EndFrame()` acquire-release; that is `GRAPHICS-036C`/`GRAPHICS-036D`.
- No changes to `Runtime.RenderExtraction`, `IRenderer::SubmitRuntimeSnapshots`,
  or `RuntimeRenderSnapshotBatch` shape.
- No multi-threading of extraction or rendering in this slice.

## Context
- Owner layer: `runtime` (composition/lifetime). The pool imports nothing from
  graphics; it manages only slot indices and their lifecycle, so no new
  dependency edge is introduced (`AGENTS.md` §2/§4).
- The `GRAPHICS-036` planning decisions referenced a conceptual
  `ImmutableRenderWorld` type. That type does not exist in the tree: the real
  snapshot is the span-based `RuntimeRenderSnapshotBatch` (graphics layer),
  backed by runtime-owned stable storage in `RenderExtractionCache`. Because the
  snapshot is non-owning views, the *payload* binding is a larger change owned by
  `GRAPHICS-036C`; this slice lands the slot-lifecycle core the planning slice
  calls "atomic swap primitives + reclamation queue", operating on slot indices
  exactly as the recorded test seam (decision 9) describes:
  `AcquireBack(A) → PublishFront(A) → AcquireFront→A → AcquireBack(B) → …`.
- The `GRAPHICS-036-Impl-A/B/C/D` child names from the planning slice map to the
  validator-valid task IDs `GRAPHICS-036A/B/C/D` (the repo file-ID convention,
  matching `GRAPHICS-029A`, `GRAPHICS-033E`, `GRAPHICS-076E`).
- Honors `GRAPHICS-036` decisions 1 (default 3 buffers, configurable down to a
  single synchronous slot), 3 (acquire/publish/acquire/release rotation with an
  atomic release/acquire front index and atomic per-slot refcounts), 4
  (reclamation only when refcount 0 and not the published front; drained at
  `AcquireBack`), 5 (producer-faster=replace+skip-count, consumer-faster=reuse
  current front+stall-count), 7 (the three counters live on the pool here and
  are wired through `RenderDiagnostics` by `GRAPHICS-036B`), and 9 (deterministic
  index-based test seam).

## Required changes
- [x] Add `src/runtime/Runtime.RenderWorldPool.cppm` exporting
      `Extrinsic::Runtime::RenderWorldPool` + `RenderWorldPoolDiagnostics`.
- [x] Add `src/runtime/Runtime.RenderWorldPool.cpp` with the non-trivial
      state-machine bodies (`AGENTS.md` §5).
- [x] Wire both files into `src/runtime/CMakeLists.txt`.
- [x] Add `tests/contract/runtime/Test.RenderWorldPool.cpp` and register it in
      `tests/CMakeLists.txt`.

## Tests
- [x] `contract;runtime` — buffer-count clamp, single-slot synchronous collapse,
      triple-buffer rotation, refcount lifecycle, reclamation against a held
      front, producer-faster replace+skip counter, consumer-faster reuse+stall
      counter, frame-age delta, pre-publish empty-front behavior.
- [x] CPU gate: `ctest --test-dir build/ci -R RenderWorldPool --output-on-failure`
      (10/10) and the full `contract;runtime` label set stay green.

## Docs
- [x] Note the pool value type + slot-lifecycle contract in `src/runtime/README.md`.

## Acceptance criteria
- [x] `RenderWorldPool` builds and the new `contract;runtime` cases pass under the
      default CPU gate.
- [x] No graphics/ECS imports added to the module; layering check stays clean.
- [x] Module inventory regenerated and committed.
- [x] Follow-ups `GRAPHICS-036B/C/D` remain unopened and are named here for closure.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci -R RenderWorldPool --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding any graphics/ECS/platform import to the pool module.
- Opening `GRAPHICS-036B/C/D` in this slice.

## Maturity
- Target: `CPUContracted` for the slot-lifecycle contract.
- `Operational` owned by `GRAPHICS-036C` (payload binding + renderer
  `BeginFrame`/`EndFrame` acquire-release).
- This slice closes `Scaffolded → CPUContracted` for the pool state machine.
  Diagnostics surfacing through `RenderDiagnostics` is owned by `GRAPHICS-036B`;
  the pipelined-vs-synchronous integration proof by `GRAPHICS-036D`.
