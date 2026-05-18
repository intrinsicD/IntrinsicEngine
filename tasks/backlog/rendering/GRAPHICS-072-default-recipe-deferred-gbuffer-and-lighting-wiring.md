# GRAPHICS-072 — Default-recipe deferred GBuffer + lighting pass wiring

## Goal
- Wire `Pass.Deferred.GBuffers` and `Pass.Deferred.Lighting` (existing `.cpp/.cppm` files under `src/graphics/renderer/Passes/`) into the renderer executor under the default recipe: pipelines for the gbuffer surface variant and the deferred-lighting fullscreen kernel created at renderer init / `RebuildOperationalResources()`, instances owned by `NullRenderer`, executor routes through new `RecordGBufferPass(...)` and `RecordDeferredLightingPass(...)` helpers, and the `SceneNormal`/`Albedo`/`Material0`/`SceneDepth` framegraph attachments are produced and consumed correctly.

## Non-goals
- No clustered light binning (`GRAPHICS-039` planning, future implementation).
- No virtual shadow maps (`GRAPHICS-047` planning).
- No PBR feature completeness beyond the existing material model (`GRAPHICS-042` planning).
- No transparent / special-forward sub-buckets (`GRAPHICS-025` planning).
- No new shaders; reuses `surface_gbuffer.frag`, `deferred_lighting.frag`, and `surface.vert`.

## Context
- Status: not started.
- Owner/layer: `graphics/renderer`.
- Planning anchors: `tasks/done/GRAPHICS-008-depth-surface-gbuffer-passes.md`, `tasks/done/GRAPHICS-009-deferred-lighting-and-shadows.md`, `tasks/done/GRAPHICS-009Q-lighting-shadow-clarifications.md`, `tasks/done/GRAPHICS-073-default-recipe-shadow-pass-wiring.md` (Slice C scope folded in: `set 0, binding 1` shadow-sampler binding + cross-pass `DepthAttachment → ShaderRead` barrier-transition test).
- Today: `Pass.Deferred.GBuffers.cpp` and `Pass.Deferred.Lighting.cpp` exist as command-body shells but are not owned, not piped through the executor, and have no pipelines created. `ShadowSystem` already owns the atlas + `sampler2DShadow`-bindable sampler (GRAPHICS-073 Slice B), so the binding work here is descriptor-set wiring, not resource creation.
- Upstream gates (both done): `GRAPHICS-070` (forward surface; same surface variant, easier to land first), `GRAPHICS-073` (shadows; deferred lighting consumes shadow atlas).

## Required changes
- [ ] Add `m_GBufferPass`, `m_DeferredLightingPass`, `m_GBufferPipelineLease`, `m_DeferredLightingPipelineLease` members to `NullRenderer`.
- [ ] In `InitializeOperationalPassResources(device)`, create:
  - GBuffer pipeline from `assets/shaders/surface.vert` + `surface_gbuffer.frag` with the recorded multi-target output (`SceneNormal`, `Albedo`, `Material0`, `SceneDepth`),
  - Deferred lighting pipeline from a fullscreen vertex (existing or new minimal one) + `deferred_lighting.frag` consuming the GBuffer + light SSBO + the `ShadowSystem`-owned shadow atlas + sampler.
- [ ] Republish both pipelines byte-identical from `RebuildOperationalResources()`.
- [ ] Add executor branches for `"Pass.Deferred.GBuffers"` and `"Pass.Deferred.Lighting"` with the `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy.
- [ ] Confirm `BuildDefaultFrameRecipe` declares the deferred resources (`SceneNormal`, `Albedo`, `Material0`) with the recorded formats and finalizes them through `Pass.Deferred.Lighting`.
- [ ] Wire the deferred lighting descriptor set so the `ShadowSystem`-owned shadow atlas is bound at `set 0, binding 1` (sourced from `ShadowSystem::GetAtlasBindlessIndex()` / `GetAtlasSampler()` per `GRAPHICS-009Q`; absorbed from GRAPHICS-073 Slice C).

## Tests
- [ ] `contract;graphics` test: with one extracted procedural surface and the default recipe, both deferred passes record and `RenderGraphCommandPassStats::Recorded` includes them.
- [ ] `contract;graphics` test: barrier packets between GBuffers (write) and Lighting (read) are emitted in the expected order (`SceneNormal/Albedo/Material0` ColorAttachment → ShaderRead).
- [ ] `contract;graphics` test: barrier packets transition the `ShadowSystem`-owned shadow atlas `DepthAttachment → ShaderRead` before `Pass.Deferred.Lighting` (absorbed from GRAPHICS-073 Slice C).
- [ ] `contract;graphics` test: with one shadow-casting renderable + one light wired end-to-end through extraction, the executor reports `Recorded` for `ShadowPass` *and* `Pass.Deferred.Lighting`, and the deferred lighting descriptor set binds the shadow atlas at `set 0, binding 1` (absorbed from GRAPHICS-073 Slice C).
- [ ] `contract;graphics` test: missing pipeline lease → `SkippedUnavailable` for the offending pass.
- [ ] `contract;graphics` test: pipeline leases survive `RebuildGpuResources()`.

## Docs
- [ ] Update `src/graphics/renderer/README.md` to record deferred GBuffer + lighting as operationally wired and to record the shadow-atlas `set 0, binding 1` binding consumer (absorbed from GRAPHICS-073 Slice C).
- [ ] Update `docs/architecture/rendering-three-pass.md` if pipeline-order step numbers shift or shadow ordering relative to deferred lighting changes (absorbed from GRAPHICS-073 Slice C).

## Acceptance criteria
- [ ] Both passes record draws in the operational state and increment `Recorded`.
- [ ] Deferred lighting samples the `ShadowSystem`-owned shadow atlas correctly: descriptor set binds it at `set 0, binding 1` and the cross-pass barrier transitions it `DepthAttachment → ShaderRead` before `Pass.Deferred.Lighting` (absorbed from GRAPHICS-073 Slice C).
- [ ] No regression in `Pass.Forward.Surface` (forward and deferred can coexist gated by `RenderConfig` or recipe selection).
- [ ] No regression in CPU/null tests.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding clustered light binning, IBL, or PBR feature completeness.
- Adding transparent / special-forward sub-buckets.
- Mutating canonical material descriptor layout.

## Next verification step
- Land the pipelines + executor routes + barrier-order tests, exercise the contract tests above.
