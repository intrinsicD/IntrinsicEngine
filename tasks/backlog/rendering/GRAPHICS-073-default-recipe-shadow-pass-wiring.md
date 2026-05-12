# GRAPHICS-073 — Default-recipe `Pass.Shadows` wiring + shadow atlas allocation

## Goal
- Wire `Pass.Shadows` (`src/graphics/renderer/Passes/Pass.Shadows.cpp/.cppm`) into the renderer executor under the default recipe: shadow-atlas texture/sampler allocated by `ShadowSystem` at renderer init, the depth-only shadow pipeline created, instance owned by `NullRenderer`, and the executor route consumes the `ShadowOpaque` cull bucket per `GRAPHICS-009Q`.

## Non-goals
- No virtual shadow maps (`GRAPHICS-047` planning).
- No alpha-mask shadow casters (gates on the alpha-mask sub-bucket reserved by `GRAPHICS-008Q`).
- No clustered binning (`GRAPHICS-039` planning).

## Context
- Status: not started.
- Owner/layer: `graphics/renderer`.
- Planning anchors: `tasks/done/GRAPHICS-009-deferred-lighting-and-shadows.md`, `tasks/done/GRAPHICS-009Q-lighting-shadow-clarifications.md`.
- Today: `Pass.Shadows.cpp` exists as a shell; `ShadowSystem` exists with `ApplyTo(camera)` integration but no atlas resource; the executor lambda has no `"Pass.Shadows"` branch.
- Cascade view-projection matrices and missing-caster diagnostics are runtime/shadow-extraction owned per `GRAPHICS-009Q`; this task consumes those snapshots, it does not own them.

## Required changes
- [ ] Have `ShadowSystem::Initialize(...)` allocate the shadow-atlas texture (`D32_FLOAT`, sized per `RenderConfig::Shadows::AtlasSize` or a recorded default), create a `sampler2DShadow`-bindable sampler at `set 0, binding 1` per the `GRAPHICS-009Q` decision, and free both at `Shutdown()`.
- [ ] Add `m_ShadowPass` and `m_ShadowPipelineLease` members to `NullRenderer`. Create the depth-only shadow pipeline via `PipelineManager::Create(...)` reusing `assets/shaders/depth_prepass.vert` (or a dedicated shadow-depth shader if recorded).
- [ ] Add a `"Pass.Shadows"` branch in the executor lambda routing through `RecordShadowPass(...)` consuming the `ShadowOpaque` cull bucket and recording `BeginRenderPass(shadow-atlas) → DrawIndexedIndirectCount → EndRenderPass`.
- [ ] Confirm the deferred lighting pass (per `GRAPHICS-072`) reads the shadow atlas via `set 0, binding 1`.

## Tests
- [ ] `contract;graphics` test: with one shadow-casting renderable + one light, the shadow atlas is allocated, `Pass.Shadows` records, and the executor reports `Recorded`.
- [ ] `contract;graphics` test: barrier packets transition the shadow atlas DepthAttachment → ShaderRead before deferred lighting.
- [ ] `contract;graphics` test: missing-caster diagnostic from `ShadowSystem` is surfaced even when the cull bucket is empty (no false-positive `Recorded` status).
- [ ] `contract;graphics` test: shadow atlas survives `RebuildGpuResources()` with byte-identical handle.

## Docs
- [ ] Update `src/graphics/renderer/README.md` to record `Pass.Shadows` + atlas as operationally wired.
- [ ] Update `docs/architecture/rendering-three-pass.md` if shadow ordering shifts.

## Acceptance criteria
- [ ] `Pass.Shadows` records draws when shadow casters exist; `SkippedUnavailable` when atlas/pipeline missing.
- [ ] Deferred lighting samples the atlas correctly (test asserts the descriptor binding).
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
- Implementing virtual shadow maps.
- Adding alpha-mask shadow caster filtering.
- Owning cascade matrices in graphics (runtime extraction owns those).

## Next verification step
- Allocate the atlas, create the pipeline, wire the executor route, exercise the contract tests above.
