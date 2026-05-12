# GRAPHICS-032C — `Pass.Present.MinimalDebug` body and end-to-end CPU acceptance test

## Goal
- Implement the property-based CPU-mock command-recording body for `Pass.Present.MinimalDebug` (bind present pipeline, `Draw(3, 1, 0, 0)`) and add the end-to-end CPU acceptance test that runs `recipe → BeginFrame → SubmitRuntimeSnapshots → ExtractRenderWorld → PrepareFrame → ExecuteFrame → EndFrame` and asserts the command stream order, recorded barriers, and counter values for one frame of `MinimalDebugSurface` with one procedural triangle.

> **Scaffold notice.** `Pass.Present.MinimalDebug` and the end-to-end CPU acceptance driver authored here are removed by [`GRAPHICS-081`](GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) once the canonical `Pass.Present` is operationally wired (`GRAPHICS-076`) and the default-recipe CPU acceptance covers the same shape. Author the test driver so the assertion helpers it introduces are reusable by the default-recipe equivalent — the helpers stay; the minimal-recipe driver invocation is what gets deleted.

## Non-goals
- No `gpu;vulkan` smoke (that is `GRAPHICS-032D`).
- No new diagnostics counters beyond those declared by `GRAPHICS-032A`.
- No mutation of the default recipe's skip/no-op statuses.

## Context
- Status: not started.
- Owner/layer: `graphics/renderer`.
- Planning parent: [`tasks/done/GRAPHICS-032-minimal-surface-present-command-path.md`](../../done/GRAPHICS-032-minimal-surface-present-command-path.md), Recorded as Impl-C in the parent's Required changes.
- Upstream gates: `GRAPHICS-032B`, `GRAPHICS-031A` (the present pipeline can reuse the slot-0 fullscreen-triangle path or be a dedicated minimal-present pipeline; the implementer chooses, the contract is the recorded `Draw(3,1,0,0)` form).
- Note: `Pass.Present.MinimalDebug` and the existing `Pass.Present` (`Pass.Present.cpp:14`) share the same fullscreen-triangle command shape; the minimal variant exists so the minimal recipe stays self-contained.

## Required changes
- [ ] Add `class MinimalDebugPresentPass` (header in `Passes/Pass.Present.MinimalDebug.cppm`, body in `Passes/Pass.Present.MinimalDebug.cpp`) with `SetPipeline(PipelineHandle)` + `Execute(cmd)`. Body matches `PresentPass::Execute`: guard pipeline validity, `BindPipeline` + `Draw(3, 1, 0, 0)`.
- [ ] Have `NullRenderer` own a `MinimalDebugPresentPass` instance; create the pipeline (or reuse a present pipeline if one exists post-`GRAPHICS-032A`) at renderer init.
- [ ] Extend the renderer executor lambda with a `"Pass.Present.MinimalDebug"` branch routing to a new `RecordMinimalDebugPresentPass(...)` helper that returns the same `SkippedNonOperational` / `SkippedUnavailable` taxonomy and increments `MinimalPresentPassExecutions` on success and `MinimalRecipeMissingPrerequisiteCount` on missing prerequisite.
- [ ] End-to-end CPU acceptance test driver: with `CreateReferenceEngineConfig()`, the `MinimalDebugSurface` recipe, GRAPHICS-029B's reference triangle, GRAPHICS-030B's residency binding, and the GRAPHICS-031A slot-0 pipeline, drive one frame on the Null device (or a CPU mock command context) and assert:
  - `MinimalSurfacePassExecutions == 1`,
  - `MinimalPresentPassExecutions == 1`,
  - `MinimalRecipeMissingPrerequisiteCount == 0`,
  - barrier packets are emitted in the expected order (`SceneColorHDR` Undefined → ColorAttachment, `SceneDepth` Undefined → DepthStencilAttachment, `SceneColorHDR` ColorAttachment → ShaderRead before present, imported `Backbuffer` Undefined → ColorAttachment for present, `Backbuffer` ColorAttachment → PresentSrc as the end-of-graph sentinel),
  - the `RuntimeRenderExtractionStats::ProceduralGeometryUploads` is 1.

## Tests
- [ ] `contract;graphics` test: `Pass.Present.MinimalDebug` records `BindPipeline(present)` + `Draw(3, 1, 0, 0)` and only those two commands.
- [ ] `contract;graphics` test (the end-to-end driver above).
- [ ] `contract;graphics` test: with the default recipe selected, the new branch is not hit and `MinimalSurfacePassExecutions == 0`, `MinimalPresentPassExecutions == 0` (recipe-vs-default isolation).
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/graphics/renderer/README.md` to list the new `Pass.Present.MinimalDebug` module.
- [ ] Update `docs/architecture/rendering-three-pass.md` if the minimal-recipe pass-name table needs the new entry (otherwise leave untouched).
- [ ] Refresh `docs/api/generated/module_inventory.md` after adding the module.

## Acceptance criteria
- [ ] After this task, on the Null device, the `MinimalDebugSurface` frame contract is fully exercised through CPU mocks; once `GRAPHICS-033C` lands the Vulkan command-recording bodies, the same recipe targets a real swapchain.
- [ ] `MinimalSurfacePassExecutions` and `MinimalPresentPassExecutions` increment together for one frame.
- [ ] No regression in default-recipe behavior or counters.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding a `gpu;vulkan` test.
- Reusing the imported `Backbuffer` for non-present writes (validated by render-graph rejection).
- Wiring the default recipe through the minimal pass branches.

## Next verification step
- Land the present pass + acceptance driver, exercise the contract tests above.
