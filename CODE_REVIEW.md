# Code Review Notes

## Sandbox application
- `OnUpdate` computes `aspectRatio` before validating the window dimensions. If the window is minimized and `GetHeight()` returns zero, this division triggers undefined floating-point results or a crash. Guard the aspect calculation with a non-zero height check before dividing.【F:src/Apps/Sandbox/main.cpp†L108-L136】
- The hierarchy context menu calls `destroy(m_SelectedEntity)` without verifying that a valid entity is selected. When `m_SelectedEntity` is `entt::null`, `entt` asserts in debug builds and behavior is undefined in release builds. Add a validity check before destruction to avoid registry misuse.【F:src/Apps/Sandbox/main.cpp†L214-L231】

## Render graph flow
- The ImGui pass re-imports the swapchain image instead of consuming the color attachment produced by `ForwardPassData`, leaving no explicit dependency between the forward render pass and the UI overlay. Depending on the render-graph implementation, this can schedule passes in the wrong order or discard the forward pass output. Thread the color handle from the forward pass into the ImGui pass to ensure ordering and correct load/store semantics.【F:src/Runtime/Graphics/Graphics.RenderSystem.cpp†L100-L252】
