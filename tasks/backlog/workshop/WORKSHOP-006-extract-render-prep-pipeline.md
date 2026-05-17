# WORKSHOP-006 — Extract render prep pipeline from renderer

## Goal
- Move CPU-side render preparation sequencing out of the main renderer implementation into a dedicated `RenderPrepPipeline` seam with explicit inputs, outputs, dependencies, and tests.

## Non-goals
- Do not change the render graph executor.
- Do not change frame recipe construction.
- Do not implement new culling/material/light features.
- Do not remove the existing task graph path unless behavior-equivalent tests prove it unnecessary.

## Context
- `PrepareFrame` currently builds and executes a render-prep task graph inside the renderer, sequencing pipeline commit, material sync, visualization sync, transform sync, light sync, GPU-world sync, and culling sync.
- The sequence is important foundation logic and should be isolated before more systems are added.
- A dedicated seam makes it easier to test prep ordering without running full frame execution.

## Required changes
- [ ] Introduce a `Graphics.RenderPrepPipeline` module/class under `src/graphics/renderer/`.
- [ ] Define an explicit input bundle for prep dependencies, including required systems/managers and snapshot vectors.
- [ ] Define an explicit result/diagnostic type for prep success/failure.
- [ ] Move current task-graph preparation sequence into the new class.
- [ ] Preserve the current ordered steps:
  - pipeline commit
  - base material sync
  - visualization sync
  - override material sync
  - transform sync
  - light sync
  - GPU world sync
  - culling sync
- [ ] Keep the optional sequential fallback only if still needed; otherwise remove it with tests proving equivalent behavior.
- [ ] Make missing subsystem inputs fail closed with a structured diagnostic.
- [ ] Make renderer `PrepareFrame` delegate to `RenderPrepPipeline`.

## Tests
- [ ] Add tests proving prep steps execute in the required order.
- [ ] Add tests proving missing required inputs fail closed.
- [ ] Add tests proving failed task graph compile/execute reports diagnostics and leaves frame unprepared.
- [ ] Update renderer frame lifecycle tests to assert `ExecuteFrame` refuses to run after failed prep.
- [ ] Keep graphics contract tests passing.

## Docs
- [ ] Update rendering architecture docs to document the render-prep pipeline seam.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` graphics/renderer row if needed.
- [ ] Update generated module inventory if public module surfaces changed.

## Acceptance criteria
- [ ] Main renderer implementation no longer constructs render-prep task graph inline.
- [ ] Render-prep ordering is tested independently.
- [ ] Failure modes are structured and visible through renderer lifecycle diagnostics.
- [ ] No behavior changes are made to frame execution after successful prep.

## Verification
```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L "unit|contract" -LE "gpu|vulkan|slow|flaky-quarantine" --timeout 60 -j$(nproc)
```

## Forbidden changes
- Do not change render pass command bodies.
- Do not make prep depend on runtime or ECS.
- Do not hide prep failures as successful frames.
- Do not broaden this into renderer class decomposition beyond the prep seam.
