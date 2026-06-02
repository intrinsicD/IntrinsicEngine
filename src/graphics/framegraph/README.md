# Graphics Framegraph

`src/graphics/framegraph` owns backend-agnostic render-graph resource declarations,
pass declarations, compilation, validation, transient allocation, barrier packet
inference, and execution ordering. It does not own renderer recipe selection,
backend command bodies, platform windows, swapchains, runtime extraction, live ECS
state, or asset-service traffic.

## Ownership boundaries

- `graphics/renderer` owns `FrameRecipe` identities, recipe feature gates, pass
  labels, renderer diagnostics, and pass-body command contracts.
- `graphics/framegraph` consumes recipe-declared resources/passes, validates
  imported-resource write authorization, computes first/last resource uses, and
  emits barrier packets in pass order.
- `graphics/vulkan` lowers compiled graph resources/barriers/commands to native
  Vulkan objects and owns swapchain/acquire/present mechanics.
- `runtime` submits immutable render snapshots; graphics never queries live ECS
  ownership.

## Default Recipe Contract

The canonical renderer recipe is built by
`BuildDefaultFrameRecipe(graph, features, imports, sizing, shadowSizing)` in
`Extrinsic.Graphics.FrameRecipe` and declared by
`DescribeDefaultFrameRecipe(features)`. The framegraph treats those declarations
as the single source of truth for imported-resource write authorization,
transient-resource lifetime, pass ordering, and final backbuffer presentation.

The framegraph compiler infers required transitions from declared uses: draw
passes write `SceneColorHDR` and related intermediate attachments, optional
postprocess/debug/UI passes move the current `FrameRecipe.PresentSource`, and
the canonical `Present` declaration writes the imported `Backbuffer`. The
post-pass `ColorAttachmentWrite -> Present` transition is emitted from
`RenderGraph::ImportBackbuffer`'s final-state contract; there is no recipe-local
barrier annotation or special backbuffer-write exception.

Tests should assert compiled graph/resource properties by pass label and resource
name, not transient allocation IDs or backend-native handles.

## Default-Recipe `gpu;vulkan` Smoke

The opt-in default-recipe smoke coverage lives in
`tests/integration/graphics/Test.DefaultRecipeSurfaceGpuSmoke.cpp`. It drives the
canonical recipe on promoted Vulkan hosts, asserts that the operational command
stream records canonical passes such as `Present`, and keeps the four-sample
backbuffer-readback parity harness under `gpu;vulkan;graphics`. The fixture is
selected only via `ctest -L 'gpu' -L 'vulkan' ...`; the default CPU gate excludes
it and the tests report `SKIPPED` when GLFW or a Vulkan-capable swapchain/device
is unavailable.
