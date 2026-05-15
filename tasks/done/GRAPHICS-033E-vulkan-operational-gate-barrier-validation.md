# GRAPHICS-033E — Wire the `BarrierValidationClean` operational gate

## Status

- Status: in-progress (slice 2 landed; awaiting retirement once the default CPU gate is confirmed green for the producer-side fix).
- Owner/agent: Claude on `claude/setup-agentic-workflow-2glkN`.
- Branch: `claude/setup-agentic-workflow-2glkN`.
- Slice 1 landed previously on `claude/setup-agentic-workflow-oX1eU` (merged via PR #831):
  - `RHI::IDevice::NoteRecipeGraphValidation(bool)` (default no-op) added in `src/graphics/rhi/RHI.Device.cppm`.
  - `VulkanDevice` override + `std::atomic<bool> m_LatestRecipeValidationClean{false}`, reset to `false` in `Initialize()`, consumed by `BuildOperationalInputs()` for `inputs.BarrierValidationClean` in `src/graphics/vulkan/Backends.Vulkan.Device.cppm` / `.cpp`.
  - `Graphics.Renderer.cpp::ExecuteFrame()` calls `ValidateRecipeCompiledGraph(...)` after each successful `RenderGraph::Compile()` and publishes the boolean exactly once per compile attempt; the recipe-build-failure and compile-failure paths publish `false` to preserve fail-closed semantics.
  - Backend-public read accessor `GetVulkanDeviceOperationalInputs(const RHI::IDevice*)` (declared in `Backends.Vulkan.OperationalStatus.cppm`, implemented in `Backends.Vulkan.Device.cpp`) lets contract tests observe `BarrierValidationClean` without re-running the evaluator.
  - Tests: `RendererFrameLifecycle.PublishesRecipeGraphValidationOnSuccessfulCompile`, `RendererFrameLifecycle.PublishesFailClosedRecipeValidationOnRecipeBuildFailure`, `VulkanFailClosedContract.RecipeGraphValidationSetterFlipsBarrierValidationClean`.
  - Docs: `src/graphics/vulkan/README.md` §10 documents gate-7 wiring; `src/graphics/renderer/README.md` documents the `ExecuteFrame()` publish step. `MockDevice` records every `NoteRecipeGraphValidation` call.
- Slice 2 landed on this branch (producer-side bug fix):
  - The slice-1 producer wiring computed `recipeValidationClean` as `recipeValidation.CountBySeverity(Error) == 0u && !GetLastCompileValidationResult().HasErrors()`. The second clause re-consulted the bare compile-time validator, which has no recipe context and therefore reports `UnauthorizedImportedBufferWrite` for every imported write from a non-side-effect pass (e.g. `CullingPass` writing `Cull.SurfaceOpaque.IndexedArgs`). The default recipe always tripped this false positive, so gate 7 could never flip to `true` and `RendererFrameLifecycle.PublishesRecipeGraphValidationOnSuccessfulCompile` regressed under the default CPU gate.
  - Fix in `src/graphics/renderer/Graphics.Renderer.cpp::ExecuteFrame()`: publish solely on `recipeValidation.CountBySeverity(RenderGraphValidationSeverity::Error) == 0u`. The recipe-aware validator already provides the `ImportedResourceAuthorization` entries; consulting the bare compile-time result is strictly redundant and over-restrictive.
  - Doc sync in `src/graphics/renderer/README.md` to match the simplified formula.
  - No test changes: the existing `RendererFrameLifecycle.PublishesRecipeGraphValidationOnSuccessfulCompile` test (which expected `true`) now passes.
- Next verification step: default CPU gate now green on this branch (1884/1885 pass; the single failure `CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering` is a documented pre-existing flake that passes on retry, tracked in `tasks/done/GRAPHICS-031A`). Ready to retire to `tasks/done/`.

## Goal
- Flip the `BarrierValidationClean` input to the `EvaluateVulkanOperationalStatus(...)` 9-step gate from its current hard-coded `false` (`src/graphics/vulkan/Backends.Vulkan.Device.cpp:882`) so it observes the latest `RenderGraphValidationResult` for the active recipe compile and returns `true` only when the recipe-graph validation reports no `Error`-severity findings on the canonical resource set (`SceneColorHDR`, `SceneDepth`, imported backbuffer finalization, transfer-queue uploads). This is gate step 7 of the operational checklist in `src/graphics/vulkan/README.md:360–362` and is one of the two remaining gates blocking `IsOperational()` from ever returning `true`.

## Non-goals
- No new validation findings, severities, or codes; this task consumes the existing GRAPHICS-022 `RenderGraphValidationResult` taxonomy verbatim.
- No `gpu;vulkan` smoke fixture (still reserved for `GRAPHICS-033D`).
- No change to the `PublicServiceReconciled` gate (`GRAPHICS-033F`).
- No mutation of the validation-layer policy (locked by `GRAPHICS-018Q`).
- No additional pass bodies or recipe authoring; the wiring is independent of which recipe is active.
- No additional renderer → device public surfaces beyond the minimum needed to publish a CPU-public boolean. No `Vk*` types may flow across the renderer/RHI seam.

## Context
- Status: done (2026-05-15, branch `claude/setup-agentic-workflow-2glkN`).
- Owner/layer: `graphics/vulkan` (publish-side: extends `VulkanDevice::BuildOperationalInputs()` with a CPU-public boolean read), `graphics/renderer` (producer-side: publishes the validation outcome of the active recipe compile after `ValidateRecipeCompiledGraph(...)`).
- Planning parent: [`tasks/done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md`](../../done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md). The parent identified Impl-A/B/C/D but did not enumerate an explicit child for gates 7/8; `GRAPHICS-033E` is the planning-gap fill that owns gate 7.
- Upstream gates: `GRAPHICS-033C` (recording bodies present and `MinimalRecipeRecordingPresent == true`), `GRAPHICS-022` (rendergraph validation surface, `RenderGraphValidationResult` / `GetLastCompileValidationResult()`), `BUG-009` and `BUG-010` (already-landed minimal-recipe correctness fixes).
- Downstream gates: `GRAPHICS-033F` (public-service reconciliation, gate 8) and the active `GRAPHICS-080` retirement, which is blocked on gates 7 and 8 both flipping to `true`.
- The renderer compiles + validates the active recipe each frame via `ValidateRecipeCompiledGraph(...)` (`src/graphics/renderer/Graphics.FrameRecipe.cppm:167`) and stores the result on the compiled graph (`Graphics.RenderGraph.Compiler.cppm:153`, accessible through `RenderGraph::GetLastCompileValidationResult()`). The producer-side seam already exists; this task wires it through to the device's operational inputs.
- Layering: the renderer must not import `Backends.Vulkan`; the wiring crosses the seam through a backend-neutral `IDevice` setter (CPU-public boolean), not through a Vulkan-specific path.

## Required changes
- [x] Add a backend-neutral CPU-public setter on `RHI::IDevice` for "latest recipe-graph validation is clean" (working name: `void NoteRecipeGraphValidation(bool clean) noexcept`). Default implementation is a no-op; `VulkanDevice` overrides it to update an internal `bool` consumed by `BuildOperationalInputs()`.
- [x] In `Graphics.Renderer.cpp`, after each successful `ValidateRecipeCompiledGraph(...)` call, publish `result.CountBySeverity(RenderGraphValidationSeverity::Error) == 0u` to the device through the new setter. Publish exactly once per recipe compile to avoid mid-frame oscillation.
- [x] Update `VulkanDevice::BuildOperationalInputs()` to set `inputs.BarrierValidationClean = m_LatestRecipeValidationClean` (an `std::atomic<bool>` initialized to `false`) instead of the hardcoded `false` at `Backends.Vulkan.Device.cpp:882`. The boolean is set only by the renderer-published setter and is reset to `false` in `Initialize()` so cold startup remains fail-closed until the first clean compile.
- [x] Document in `src/graphics/vulkan/README.md` (under the §10 operational gate section) that gate 7 now consults the renderer-published validation outcome and that fail-closed semantics survive: a single `Error`-severity finding flips the gate to `false` until the next clean compile.
- [x] Do not introduce any `Vk*` symbol on the renderer side or any rendergraph type on the backend side. The boolean is the only data crossing the seam.

## Tests
- [x] `contract;graphics` test: `EvaluateVulkanOperationalStatus(...)` with `BarrierValidationClean = false` returns `{RequestedButIncompleteGate, BarrierValidationFailed}` (already covered by `Test.VulkanOperationalStatusEvaluator.cpp`); confirm the new producer wiring leaves that evaluator coverage byte-identical.
- [x] `contract;graphics` test: `IDevice::NoteRecipeGraphValidation(true)` followed by `BuildOperationalInputs()` yields `BarrierValidationClean == true`; `NoteRecipeGraphValidation(false)` flips it back. Test the Null-device default no-op so non-Vulkan backends remain unaffected.
- [x] `contract;graphics` test: starting from a fresh `VulkanDevice::Initialize(...)`, `BuildOperationalInputs().BarrierValidationClean == false` (cold-start fail-closed), and remains `false` until the renderer publishes a clean compile.
- [x] `contract;renderer` test: after one successful recipe compile + `ValidateRecipeCompiledGraph(...)`, the renderer calls `IDevice::NoteRecipeGraphValidation(true)` exactly once with the right boolean derived from `RenderGraphValidationResult::CountBySeverity(Error)`. A synthetic error finding flips the call argument to `false`.
- [x] No `gpu;vulkan` test in this slice (reserved for `GRAPHICS-033D`).

## Docs
- [x] Update `src/graphics/vulkan/README.md` §10 to mark gate 7 (`BarrierValidationClean`) as wired and describe the producer call site.
- [x] Update `src/graphics/renderer/README.md` to document the post-validation `NoteRecipeGraphValidation()` publish step in the executor lifecycle bullet list.
- [x] Update the truth table commentary in `docs/architecture/graphics.md` if the path from recipe-compile to gate becomes user-visible diagnostics; leave the truth table itself unchanged (the table is keyed on the gate outcome, not the producer chain).

## Acceptance criteria
- [x] On the CPU/null path, the new setter is a no-op (`NullDevice::NoteRecipeGraphValidation` does nothing observable) and the default CPU gate stays green.
- [x] On the Vulkan path, after one successful clean recipe compile the device reports `BarrierValidationClean == true` in `BuildOperationalInputs()`; injecting a synthetic `Error` finding flips it back to `false` on the next compile.
- [x] No `Vk*` symbol or rendergraph type crosses the renderer/RHI seam in either direction beyond the boolean setter.
- [x] `EvaluateVulkanOperationalStatus(...)` continues to report `{RequestedButIncompleteGate, PublicServiceReconciliationFailed}` until `GRAPHICS-033F` lands; this task does not flip the device to `Operational` on its own.
- [x] No regression in any pre-existing fallback counter, breadcrumb, or `FallbackDiagnosticsSnapshot` field.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicGraphicsRendererCpuUnitTests ExtrinsicBackendsVulkanContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding any `Vk*` type to the renderer or any rendergraph type to `Backends.Vulkan`.
- Flipping `PublicServiceReconciled` in the same patch (reserved for `GRAPHICS-033F`).
- Introducing a new validation-finding severity or code.
- Mutating the GRAPHICS-022 `RenderGraphValidationResult` shape.
- Aborting startup or frame execution when the gate is `false`; runtime continues to fall back to Null per the GRAPHICS-033 truth table.
- Mixing mechanical file moves with semantic refactors.

## Completion
- Completed: 2026-05-15.
- Commit references: slice 1 — `be7decf` (PR #831, branch `claude/setup-agentic-workflow-oX1eU`); slice 2 — local commit on `claude/setup-agentic-workflow-2glkN` that drops the over-restrictive `&& !GetLastCompileValidationResult().HasErrors()` clause from the producer publish.
- Verification (this session, clang-20 + ASan/UBSan):
  - `cmake --preset ci`.
  - `cmake --build --preset ci --target IntrinsicTests`.
  - `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` → 1884/1885 pass; the single failure `CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering` is a documented pre-existing flake (`tasks/done/GRAPHICS-031A`) and passes on retry.
  - `ctest --test-dir build/ci -R 'PublishesRecipeGraphValidationOnSuccessfulCompile|PublishesFailClosedRecipeValidationOnRecipeBuildFailure|RecipeGraphValidationSetterFlipsBarrierValidationClean'` → all 3 pass.
  - `python3 tools/repo/check_layering.py --root src --strict` → no violations.
  - `python3 tools/repo/check_test_layout.py --root . --strict` → no findings.
  - `python3 tools/docs/check_doc_links.py --root .` → 344 links, no broken.
  - `python3 tools/agents/check_task_policy.py --root . --strict` → 224 files, 0 findings.

## Next verification step
- Done. Gate 8 (`GRAPHICS-033F`) retired in parallel; downstream `GRAPHICS-080` retirement is now gated only on `gpu;vulkan` host verification (reserved for `GRAPHICS-033D`).
