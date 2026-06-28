# Frame Graph Architecture

The frame graph is the renderer-owned per-frame pass/resource contract. Runtime
builds `RenderFrameInput` and extracts immutable `RenderWorld` snapshots; the
renderer turns those snapshots into a typed frame recipe, compiles a render
graph, records backend commands, and submits the frame. Gameplay, ECS, asset
services, and platform state do not enter the graphics frame graph directly.

## Ownership

- `src/graphics/renderer/Graphics.FrameRecipe.*` owns the live frame driver.
  `FrameRecipeFeatures` selects optional pass families, `FrameRecipePassKind`
  and `FrameRecipeResourceKind` map to stable `FramePassId` /
  `FrameResourceId` values, and `BuildDefaultFrameRecipe(...)` declares the
  pass/resource graph for the current frame.
- `DeriveDefaultFrameRecipeFeatures(const RenderWorld&)` derives the default
  feature set from render-ready snapshot data. Renderer-local state may then
  refine those defaults before the graph is built.
- `DescribeDefaultFrameRecipe(...)` exposes the same recipe shape as
  introspection data for tests, debug views, UI, and config validation.
- `RenderGraph` owns the compiled pass/resource DAG and resource-state
  scheduling. It receives declarations from the recipe; it is not the authority
  for deciding which gameplay or editor features are enabled.

`FrameRecipe*` is therefore the authoritative live composition path. Any config
or UI recipe lane must project onto this path before it can affect a rendered
frame.

## Recipe Config Lane

`src/graphics/renderer/Graphics.RenderRecipeConfig.*` owns the external
recipe-config document schema. Its schema id is
`intrinsic.graphics.render-recipe-config`, with schema version `1`.
`PreviewRenderRecipeConfig(...)` and `LoadRenderRecipeConfigFile(...)` parse a
document against a `RenderRecipeConfigContext` containing the current renderer
descriptor, base render recipe, view/output recipe, and binding set.

Preview is side-effect-free: it returns a `RenderRecipeConfigLoadResult`
containing validation state, diagnostics, the parsed preview, disabled extension
slots, binding overrides, and contract diagnostics. Invalid, stale,
unsupported, or fixed-core-mutating documents fail closed.

The fixed core is intentionally guarded. Config can disable declared optional
extension slots and express supported binding/output overrides, but it cannot
inject arbitrary pass-graph nodes, rename fixed passes/resources, or mutate the
renderer's required core shape. The live projection surface is
`Graphics::FrameRecipeOverride`: it carries a validated `RenderRecipeDescriptor`,
disabled extension slots, and a source id. `ProjectFrameRecipeOverride(...)`
applies that overlay to `FrameRecipeFeatures`; unsupported or fixed-core changes
produce diagnostics instead of silently changing the graph.

## Edit Lanes

There are three current edit lanes over the same preview/apply contract:

- **Config files.** `Engine::LoadRenderRecipeConfigPreviewFile(...)` previews a
  recipe file. `Engine::LoadAndApplyRenderRecipeConfigFile(...)` applies a
  usable preview by installing a `FrameRecipeOverride` on the renderer.
- **Sandbox UI.** The `Render Recipes` sandbox editor panel validates, previews,
  activates, cancels, publishes, and applies recipe drafts through runtime-owned
  command helpers. The UI stores draft/presentation state only; it does not own
  renderer state.
- **Agent/CLI/programmatic control.** `Engine::PreviewRenderRecipeConfigDocument(...)`,
  `ActivateRenderRecipeConfigDocument(...)`, and
  `ApplyRenderRecipeConfigPreview(...)` expose the same side-effect-free preview
  and fail-closed apply path to agents, CLI tools, tests, and application code.

Runtime config control also exposes `render.default_recipe_config_path`.
`Engine::Initialize()` attempts the configured boot recipe after renderer
initialization. Live hot-apply is deliberately limited to that render-config
field: `ApplyEngineConfigHotSubset(...)` validates the referenced recipe before
mutating the active engine config, and rejects boot-only field changes.

## Frame Lifecycle

On each frame, runtime drives the renderer through:

1. `IRenderer::BeginFrame(...)`.
2. Runtime extraction publishes a `RenderWorld` snapshot.
3. The renderer derives default `FrameRecipeFeatures` from that snapshot.
4. Any active `FrameRecipeOverride` is projected onto those features.
5. `BuildDefaultFrameRecipe(...)` declares the pass/resource DAG.
6. `RenderGraph` compiles the DAG and the renderer records command bodies.
7. `IRenderer::EndFrame(...)` publishes completion diagnostics.

This means recipe-config activation changes the next frames by altering
feature gates through the live `FrameRecipe*` driver. The default recipe is
still rebuilt every frame from the current snapshot and renderer state; the
active override is an overlay, not a replacement render graph.

## Boundaries

- Graphics owns frame recipes, render-graph compilation, resource transitions,
  command recording, backend submission, and frame diagnostics.
- Runtime owns application lifecycle, config-control facades, editor commands,
  scene/asset composition, extraction, and renderer override installation.
- The sandbox editor and agent/CLI surfaces may preview and request activation,
  but they do not mutate renderer internals directly.
- `RenderRecipeConfig` is a document/config overlay. It is not a public pass
  injection API and does not bypass `BuildDefaultFrameRecipe(...)`.
- Frame graph inputs are render-ready snapshots, handles, and renderer-owned
  imports. Live ECS registries, live `AssetService` traffic, platform windows,
  and backend-native `Vk*` types do not cross into recipe/config public APIs.

## Related References

- Graphics subsystem context: [graphics.md](graphics.md).
- Runtime config control: [runtime-config-control.md](runtime-config-control.md).
- Runtime composition and recipe activation facade: [runtime.md](runtime.md).
- Legacy-background rendering strategy: [rendering-three-pass.md](rendering-three-pass.md).
