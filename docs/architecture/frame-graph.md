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
  pass/resource graph for the current frame by registering the default overlay
  contribution family before graph construction.
  `BuildDefaultFrameRecipeWithContributions(...)` is the explicit seam for
  tests and renderer code that need a specific contribution registry; an empty
  registry compiles the fixed core without overlay passes.
- `DeriveDefaultFrameRecipeFeatures(const RenderWorld&)` derives the default
  feature set from render-ready snapshot data. Renderer-local state may then
  refine those defaults before the graph is built.
- `DescribeDefaultFrameRecipe(...)` exposes the same recipe shape as
  introspection data for tests, debug views, UI, and config validation.
- `FrameRecipePassContributionRegistry` is the renderer-layer code seam for
  typed pass descriptors. `ValidateFrameRecipePassContributions(...)` rejects
  disabled-anchor, duplicate-ID, fixed-core-conflict, and unknown-resource
  descriptors before `DescribeFrameRecipeWithContributions(...)` projects them
  into introspection. `RegisterDefaultFrameRecipeOverlayContributions(...)`
  registers SelectionOutline, DebugView, ImGui, and VisualizationOverlay as
  code-level contributions; `FrameRecipePresentSourceResourceId()` is the typed
  pseudo-resource for their dynamic current-color input/output. This is not a
  document/config injection surface.
- `RenderGraph` owns the compiled pass/resource DAG and resource-state
  scheduling. It receives declarations from the recipe; it is not the authority
  for deciding which gameplay or editor features are enabled. The compiled graph
  preserves the recipe's typed pass and resource identities beside diagnostic
  names so renderer command routing and sampled-resource binding do not depend
  on per-frame name scans.

`FrameRecipe*` is therefore the authoritative live composition path. In this
vocabulary, `FrameRecipe*` means the per-frame pass/resource graph driver, while
`RenderRecipe*` means the renderer-independent contract/config overlay. Any
config or UI recipe lane must project onto `FrameRecipeFeatures` before it can
affect a rendered frame.

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

Runtime config control also exposes `render.default_recipe_config_path` and
`sandbox.progressive_poisson`.
`Engine::Initialize()` attempts the configured boot recipe after renderer
initialization. Live hot-apply is deliberately limited to those fields:
`ApplyEngineConfigHotSubset(...)` validates the referenced recipe before
mutating the active engine config, updates sandbox playground state as value-only
config, and rejects boot-only field changes.

## Frame Lifecycle

On each frame, runtime drives the renderer through:

1. `IRenderer::BeginFrame(...)`.
2. Runtime extraction publishes a `RenderWorld` snapshot.
3. The renderer derives default `FrameRecipeFeatures` from that snapshot.
4. Any active `FrameRecipeOverride` is projected onto those features.
5. The renderer registers the default overlay contribution family and builds a
   structural compile key from the post-override features, sizing, imported
   resource availability/shape, AA/shadow/temporal options, and contribution
   descriptors.
6. If that key changed, the renderer calls
   `BuildDefaultFrameRecipeWithContributions(...)` to redeclare the
   pass/resource DAG and `RenderGraph::Compile()` to refresh the cached
   `CompiledRenderGraph` plus matching recipe introspection. If the key is
   unchanged, the renderer reuses the cached compiled graph.
7. The renderer validates the active recipe/compiled pair, binds per-frame
   transient resources and current imported handles for the current frame slot,
   records command bodies, and submits the frame.
8. `IRenderer::EndFrame(...)` publishes completion diagnostics.

This means recipe-config activation changes the next frames by altering
feature gates through the live `FrameRecipe*` driver. The default recipe is
rebuilt on key-relevant change rather than unconditionally every frame; the
active override is an overlay, not a replacement render graph. The renderer
reports per-frame compile attempt/cache-hit counters through
`RenderGraphFrameStats::Compile`. The multi-KB compiler debug dump is lazy:
default frames leave `RenderGraphFrameStats::DebugDump` empty, and explicit
renderer debug-dump enablement builds it from the current compiled graph.

## Transient Placement

The framegraph compile product includes a deterministic placement plan for used
non-imported transient textures and buffers. Placement is driven by the same
first/last pass lifetime intervals used for logical transient handle reuse. Each
placement records `{block, offset, size, alignment}` plus the owning resource and
lifetime. The compiler exposes both the aligned naive sum and the planned peak
through `CompiledRenderGraph::TransientNaiveMemoryEstimateBytes` and
`CompiledRenderGraph::TransientPlacedPeakMemoryEstimateBytes`; renderer frame
stats mirror those fields. `TransientMemoryEstimateBytes` remains as the legacy
single estimate and currently equals the planned peak.

`SetTransientAliasingEnabled(false)` is the CPU and debug fallback lane: it
keeps the placement records but disables range reuse, so planned peak equals the
naive sum and alias-reuse hazard packets are absent. With aliasing enabled, a
range can be reused only after the prior occupant's last topological execution
rank is strictly before the new occupant's first execution rank. Reuse emits a
`TextureAliasReuseBarrierPacket` or `BufferAliasReuseBarrierPacket` on the
new occupant's `BeforePass` barrier packet, keyed to the real pass index after
the execution-rank placement pass is complete.

The renderer lowers the compiled plan through the RHI placed-memory seam only
when renderer transient aliasing is explicitly enabled and the device reports
compatible requirements. The renderer recomputes placements from backend
requirements, creates per-frame memory blocks, binds transient textures/buffers
at planned offsets, and submits alias-reuse memory barriers before the first pass
that reuses a range. If
aliasing is disabled or any requirement, memory-block, or placed-resource
operation fails, the renderer clears alias-reuse barriers and uses the
non-aliased fallback lane: concrete per-frame RHI textures and buffers cached per
resource index. The opt-in Vulkan smoke for `GRAPHICS-118` is the operational
evidence: aliasing-off/naive was 263168 bytes, aliasing-on/placed peak was
197632 bytes, readback matched aliasing-off output, and validation counters were
stable across the aliasing-on frame.

## Parallel Command Recording

`RenderGraphExecutor::ExecuteParallelRecordJoin(...)` is the backend-neutral
record/join primitive. It records live passes by
`CompiledRenderGraph::TopologicalLayerByPass`, then emits barrier and submit
callbacks in the same serial topological order as `Execute(...)`.

The renderer exposes `IRenderer::SetParallelRenderGraphRecordingEnabled(...)`
as a debug/contract selector. When disabled, the historical serial path is used.
When enabled on a single-queue frame, the renderer asks
`RHI::IDevice::BeginFrameParallelCommandContexts(...)` for a frame-scoped
per-pass command-context plan. If the device declines or does not support
parallel command contexts, `RenderGraphFrameStats::Execute.SerialFallbackUsed`
is set and the renderer records through the serial graphics context. If the
device accepts, the current path records each pass into its acquired context
without worker fan-out, then calls
`IDevice::SubmitParallelCommandContext(...)` in compiled serial order while
barriers, post-graph readbacks, and runtime frame hooks remain on the primary
graphics context. This proves the RHI acquisition and deterministic submit
contract without racing renderer-owned pass state.

`RenderGraphFrameStats::Execute` reports whether the executor used scheduler
workers. Command-record diagnostics now accumulate through a guarded frame-local
renderer accumulator and publish to `RenderGraphFrameStats::CommandRecords` after
record/join completion. Picking and histogram readback issue metadata now route
through guarded renderer helpers before the render-thread `BeginFrame()` drains
consume it. Transient-debug, visualization-overlay, and ImGui dynamic upload
helpers serialize per-frame reset plus pass-body upload/execute sections behind
a shared renderer guard. Postprocess pass helpers also serialize per-frame
bloom scratch, histogram viewport/buffer, and AA stage pass-object recording.
The current renderer path still pins scheduler use false because the Vulkan
backend still allocates accepted secondary buffers from externally synchronized
frame command pools. Later `GRAPHICS-119` slices must isolate that remaining
surface before enabling `Core::Tasks` fan-out.

Null provides CPU bookkeeping contexts for this contract. Vulkan accepts the
current graphics-queue plan shape with backend-local secondary command buffers
and records `vkCmdExecuteCommands(...)` into the primary context at each serial
submit callback; the secondary buffers are retained until the frame-slot fence
has retired and are freed on the next `BeginFrame`. Non-graphics queue fan-out,
worker fan-out through `Core::Tasks`, benchmark evidence, and opt-in
`gpu;vulkan` smoke coverage remain later `GRAPHICS-119` slices.

## Boundaries

- Graphics owns frame recipes, render-graph compilation, resource transitions,
  command recording, backend submission, and frame diagnostics.
- Runtime owns application lifecycle, config-control facades, editor commands,
  scene/asset composition, extraction, and renderer override installation.
- The sandbox editor and agent/CLI surfaces may preview and request activation,
  but they do not mutate renderer internals directly.
- `RenderRecipeConfig` is a document/config overlay. It is not a public pass
  injection API and does not bypass `BuildDefaultFrameRecipe(...)`. Code-level
  renderer contributions must use typed `FramePassId` / `FrameResourceId`
  descriptors, the `FrameRecipe.PresentSource` pseudo-resource token when
  needed, and the frame-recipe contribution validator instead.
- Frame graph inputs are render-ready snapshots, handles, and renderer-owned
  imports. Live ECS registries, live `AssetService` traffic, platform windows,
  and backend-native `Vk*` types do not cross into recipe/config public APIs.

## Related References

- Graphics subsystem context: [graphics.md](graphics.md).
- Runtime config control: [runtime-config-control.md](runtime-config-control.md).
- Runtime composition and recipe activation facade: [runtime.md](runtime.md).
- Legacy-background rendering strategy: [rendering-three-pass.md](rendering-three-pass.md).
