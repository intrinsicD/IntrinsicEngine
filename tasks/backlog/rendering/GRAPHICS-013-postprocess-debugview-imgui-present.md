# GRAPHICS-013 — Post-process, debug view, ImGui, and present passes
## Goal
- Complete post-process chain, debug-view output, ImGui overlay, and explicit present/finalization pass contracts.
## Non-goals
- No new effects beyond bloom, FXAA, SMAA, tone mapping, histogram, debug view, ImGui overlay, and present.
- No platform window ownership in graphics.
- No Vulkan-only mandatory tests.
## Context
- Owner: `src/graphics/renderer/Passes`, framegraph, and RHI/backend seams.
- Legacy post-process/debug-view/ImGui/presentation modules are behavioral references only.
## Required changes
- Fill bloom, FXAA, SMAA, tone-map, histogram, debug-view, ImGui, and present command/resource contracts.
- Declare auxiliary GPU resources for bloom temporaries, SMAA lookup textures, histogram readbacks, debug-view sampled resources, and ImGui draw-data imports.
- Make HDR-to-LDR ownership, temporary resources, and final backbuffer writes explicit in the frame recipe.
- Add diagnostics for missing HDR/LDR/backbuffer resources and unsupported effect combinations.
## Tests
- Add pass-order, resource-lifetime, load/store, dispatch/draw/copy/blit, and disabled-effect tests using null/mock backend seams.
- Add optional GPU/Vulkan smoke tests behind labels when backend behavior is implemented.
## Docs
- Update canonical post-process chain, debug-view behavior, ImGui ownership, and backbuffer policy.
## Acceptance criteria
- `SceneColorHDR` is converted to `SceneColorLDR` through explicit post-process passes.
- Only present/finalization writes the imported backbuffer.
- Debug view and ImGui overlays are explicit and testable.
## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Moving platform/window ownership into graphics.
