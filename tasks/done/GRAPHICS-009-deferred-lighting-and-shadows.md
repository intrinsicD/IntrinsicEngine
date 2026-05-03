# GRAPHICS-009 — Deferred lighting and shadows

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-03 after `GRAPHICS-008` completion.
- Completed: 2026-05-03.
- PR/commit: pending local commit.
- Completed slice: CPU/null-testable light environment diagnostics, shadow params/cascade/atlas metadata, shadow-pass command gating, deferred fullscreen lighting command contract, docs, and contract tests.
- Follow-up questions: `tasks/backlog/rendering/GRAPHICS-009Q-lighting-shadow-clarifications.md`.

## Goal
- Complete non-legacy lighting environment, shadow atlas, shadow pass, and deferred lighting pass contracts.
## Non-goals
- No clustered lighting, IBL, area lights, or new light types beyond the current packet contract.
- No Vulkan-only mandatory tests.
- No legacy shadow pass dependency.
## Context
- Owner: `src/graphics/renderer`, `src/graphics/renderer/Passes`, and shader/pipeline registry seams.
- Depends on render snapshot contracts, frame recipe, material/pipeline registry, culling buckets, and surface/G-buffer resources.
## Required changes
- Define and upload directional/ambient light packets, shadow params, cascade data, and atlas resource metadata.
- Define extraction from CPU light descriptions into `LightEnvironmentPacket` and GPU light-buffer records; graphics must consume light snapshots and must not query live ECS light components.
- Define fallback/diagnostic behavior for unsupported light types, missing shadow-caster data, and disabled lighting paths.
- Fill `LightSystem`, `ShadowSystem`, `Pass.Shadows`, and `Pass.Deferred.Lighting` command/resource behavior.
- Add structured diagnostics for unsupported cascade counts, missing depth resources, and disabled shadow state.
## Tests
- Add contract tests for light defaults, cascade packet defaults, atlas sizing, shadow bucket use, and fullscreen lighting dispatch/draw behavior.
- Add optional GPU/Vulkan smoke tests only behind appropriate labels.
- Label CPU contract tests `contract;graphics` for the default CPU gate; label optional shadow/lighting smoke tests `gpu;vulkan` so they stay opt-in.
## Docs
- Update lighting, shadow, and deferred composition sections in `docs/architecture/rendering-three-pass.md`.
## Acceptance criteria
- Directional/ambient lighting and shadow atlas contracts are non-legacy tested.
- Graphics has no live ECS light access and unsupported light types produce deterministic fallback diagnostics.
- Deferred lighting consumes canonical G-buffer resources and produces `SceneColorHDR`.
- Disabled shadows avoid unnecessary resources and commands.
## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests -j 4
ctest --test-dir build/ci --output-on-failure -R 'GraphicsLightingShadowContracts|GraphicsSurfacePassContracts|GraphicsCullingContracts|FrameRecipeContract|RenderWorldContract' --timeout 60
# Optional when hardware/driver support is available:
ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan' --timeout 120
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Making GPU/Vulkan tests part of the default CPU-supported gate.
