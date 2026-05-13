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
- `runtime` selects recipes and submits immutable render snapshots; graphics never
  queries live ECS ownership.

## GRAPHICS-032 minimal recipe contract

GRAPHICS-032A records `FrameRecipe::MinimalDebugSurface` as an opt-in
renderer-owned recipe contract with the stable label
`recipe.minimal-debug-surface`. The recipe is built by
`BuildMinimalDebugSurfaceRecipe(graph, imports, sizing)` in
`Extrinsic.Graphics.FrameRecipe`, declared by `DescribeMinimalDebugSurfaceRecipe()`,
and reachable from the renderer through the
`Core::Config::FrameRecipeKind::MinimalDebug` selector wired into
`IRenderer::SetFrameRecipe()`. The framegraph treats it like any other recipe:
two pass declarations (`Pass.Surface.MinimalDebug` then
`Pass.Present.MinimalDebug`), transient `SceneColorHDR` and `SceneDepth`
resources, and one imported `Backbuffer` writable only by the present declaration.

The framegraph compiler infers the required transitions from declared uses:
`SceneColorHDR` color-attachment write in the surface pass to sampled input in the
present pass, and a transient depth-attachment lifetime for `SceneDepth`. The
minimal recipe does not introduce recipe-local barrier annotations or special
backbuffer-write exceptions.

Recipe-vs-default isolation is mandatory: compiling the minimal recipe must not
mutate or globally reconfigure the default recipe's pass set, feature gates,
validation expectations, or skip/no-op statuses. Tests for the minimal recipe
should assert compiled graph/resource properties by pass label and resource name,
not transient allocation IDs or backend-native handles.

> **Scaffold notice.** `BuildMinimalDebugSurfaceRecipe`, the
> `MinimalDebug{Surface,Present}` pass labels, and the three minimal-recipe
> diagnostics counters introduced alongside this recipe are bootstrap-only and
> are removed by `GRAPHICS-081` once the canonical default recipe records every
> pass body operationally (`GRAPHICS-070..076`).
