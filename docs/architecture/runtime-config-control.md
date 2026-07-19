# Runtime Config Control

`Extrinsic.Runtime.EngineConfigControl` owns the live config-control facade for
non-ImGui callers and the Sandbox Editor. It is an app-composed
`IRuntimeModule`, owns the application-section registry, and publishes its exact
instance through `ServiceRegistry`. Sandbox constructs the control before boot,
resolves boot config through `control.SectionRegistry()`, then moves that same
object into `Engine::AddModule(...)`. Live callers resolve the optional service
with `engine.Services().Find<EngineConfigControl>()`.

`Engine` retains the active `EngineConfig` value and the boot-critical recipe
activation substrate. `Engine::Initialize()` resets recipe activation and
conditionally loads `render.default_recipe_config_path` through the shared
`Runtime.RenderRecipeActivation` free functions even when the live-control
module is omitted. When composed, `EngineConfigControl::OnRegister` copies that
already-applied startup state into its persistent state, retargets the borrowed
activation capability, and fully binds before publishing the service.
`OnResolve` validates the publication without replaying startup apply.
Shutdown resets the active override, withdraws the exact service instance, and
clears all borrowed bindings so stale direct references fail closed.

The facade is a set of typed subsystem methods and DTOs, not a command bus.

## Entry Points

Render-recipe control:

- `PreviewRenderRecipeConfigDocument(document, sourceId)` previews a recipe
  document against the current renderer descriptor and base recipe context.
- `LoadRenderRecipeConfigPreviewFile(path)` loads and previews a recipe file
  without mutating renderer state.
- `ActivateRenderRecipeConfigDocument(document, sourceId, source)` previews and
  applies a document through the same runtime activation path.
- `ApplyRenderRecipeConfigPreview(loadResult, source)` installs a validated
  `Graphics::FrameRecipeOverride` on the renderer and records
  `RuntimeRenderRecipeState`.
- `LoadAndApplyRenderRecipeConfigFile(path, source)` is the startup/programmatic
  file helper.

Engine-config control:

- `PreviewEngineConfigControlDocument(document, sourceId)` and
  `LoadEngineConfigControlFile(path)` reuse
  `Extrinsic.Core.Config.EngineLoad` with the live engine config as defaults.
- `ApplyEngineConfigHotSubset(loadResult, source)` applies only the current hot
  subset.
- `LoadAndApplyEngineConfigHotSubsetFile(path, source)` is the file helper.
- `GetEngineConfigControlState()` returns the live config snapshot and last
  apply result.

## Hot Subset

The live engine-config fields in the current subset are:

- `render.default_recipe_config_path`
- registered `app.sections` records

Applying a non-empty path first loads and validates the referenced render recipe
file. Only a usable recipe result reaches
`EngineConfigControl::ApplyRenderRecipeConfigPreview`, so an invalid file
rejects the engine-config hot apply without clearing a previously active recipe
override. Applying an empty path clears the active override and returns to the
derived default frame recipe.

`Extrinsic.Runtime.SandboxConfigSections` owns the typed codecs for the current
two Sandbox records; `Extrinsic.Sandbox.ConfigSections` composes their
registrations in the application before config boot. The
`sandbox.progressive_poisson` payload carries the interactive playground's
reference-sampler knobs, prefix/color-channel selection, mesh surface-sampling
input controls, and auto-run debounce settings. Applying it updates only the
active generic record; the Sandbox Editor decodes that state and
drives METHOD-012 through its runtime command surface. The block has no renderer
side effects and no direct RHI/device traffic.

The `sandbox.parameterization` payload carries one of the implemented CPU strategy
tokens (`lscm`, `harmonic_cotangent`, `tutte_uniform`, or `bff`) and the typed
LSCM, harmonic, and BFF values described in
[engine config](engine-config.md). Harmonic boundary policy uses `circle`,
`square`, or `custom`; BFF boundary mode uses `automatic_conformal`,
`target_lengths`, or `target_angles`. Its nested `view` value also carries the
`cpu_layout`/`gpu_shaded` presentation request, background choice, and
distortion-heatmap toggle. Applying the block updates only the active value-type
record. The configured sandbox parameterization command reads this same live
state through `GetParameterizationConfig`; the parameterization editor reads
the same view state when it submits the optional renderer request. There is no
second UI-only parameter path, and the GPU view selector is not a
parameterization solver-backend selector.

Section records are compared by canonical name/schema/version/payload.
`RuntimeEngineConfigApplyResult::ChangedSectionNames` is lexically ordered and
`SectionChanged(name)` queries it. Apply first rejects boot-only differences
and preflights a changed render-recipe path. It then commits the recipe path
and complete section vector, records the live snapshot/result, and invokes each
changed registration's optional callback exactly once. Callbacks are
non-failing post-commit notifications and must not re-enter config control.
Preview, rejected, and no-change operations invoke none.

Every other engine-config difference is boot-only and is reported in
`RuntimeEngineConfigApplyResult::RejectedBootOnlyFields`; the live config remains
unchanged.

## UI And Agent Parity

`Extrinsic.Sandbox.Editor.Shell` keeps the ImGui widget and draft-buffer
state. Its `Runtime::SandboxEditorSession` prepares an attachment-guarded
`SandboxEditorContext` through
`Extrinsic.Runtime.SandboxEditorFacades`; the shell's preview and
activation handlers call the same callbacks carried by that context after the
session resolves `EngineConfigControl` from `Engine::Services()`:

- preview routes to
  `EngineConfigControl::PreviewRenderRecipeConfigDocument`;
- activation routes to
  `EngineConfigControl::ApplyRenderRecipeConfigPreview` with
  `RuntimeRenderRecipeActivationSource::Editor`.
- progressive-Poisson knob edits route to
  `EngineConfigControl::PreviewEngineConfigControlDocument` and
  `EngineConfigControl::ApplyEngineConfigHotSubset` with
  `RuntimeConfigControlSource::Editor`; the facade updates the typed draft
  through `SetProgressivePoissonPlaygroundConfig`.
- The parameterization panel delivered by retired `UI-036` routes strategy,
  value, render-mode, background, and heatmap edits through those same
  engine-config preview/apply methods with
  `RuntimeConfigControlSource::Editor` after
  `SetParameterizationConfig`; the configured parameterization facade and
  UV-view request path consume the resulting live config.

Agent/CLI callers use the same `EngineConfigControl` methods with
`RuntimeConfigControlSource::AgentCli` or
`RuntimeRenderRecipeActivationSource::AgentCli`. The facade is CPU/headless and
does not require any ImGui frame. If the module is omitted, editor recipe and
engine-config states are null, their command callbacks remain empty, and both
availability flags are false.

## Parameterization Editor Facade

`Extrinsic.Runtime.SandboxEditorFacades` exposes the CPU parameterization
operation through the existing editor/session context. The direct command
accepts a typed `Runtime::ParameterizationConfig` and fails closed on
invalid values; the configured command reads
the registered `sandbox.parameterization` record, so editor, agent/CLI, and
programmatic applies drive identical strategy conversion and geometry
execution. Stable config tokens are converted at the runtime boundary to the
typed `Geometry.Parameterization` variant. Runtime does not serialize variant
indices or add a backend-policy layer for the one available CPU family.

On success the command writes one UV per stored vertex to the selected mesh's
`v:texcoord` property, marks vertex texcoords and attributes dirty, and records
the prior values in `EditorCommandHistory`; undo and redo therefore cover the
same mesh-property mutation. The result is pointer-free and reports the chosen
strategy value (with a stable-token helper), normalized status, and aggregate
parameterization diagnostics.

The companion `SandboxEditorParameterizationViewModel` copies the selected
mesh into UI-safe CPU data: per-vertex UVs, deterministic triangle index
triples from the canonical runtime surface packer, finite UV bounds, and
aggregate last-result diagnostics. When GPU shading is selected it also builds
expanded line indices and a per-triangle projection of the canonical
face-storage-aligned conformal-distortion payload; the default CPU path avoids
that GPU-only dense work. It carries no raw
mesh/property pointers and invents no chart or seam records. Retired `UI-036`
turns this model into the visible split UV view. The optional GPU path uses a
pointer-free runtime command surface to resolve the selected surface's existing
`GpuGeometryHandle` and optional resident albedo binding, submit a copied
renderer request, and return only presentation state plus a bindless texture
index. The panel refreshes the request once per visible frame, so closing or
globally hiding the window disables the optional pass on the next renderer
prepare instead of retaining unseen GPU work. A non-operational device,
missing residency, invalid request, resource
failure, or target that has not completed for the current token/extent reports
an explicit CPU-layout fallback; the app never receives the graphics texture
handle. This preserves the derived-view decision in
[ADR-0025](../adr/0025-parameterization-uv-view-and-split-view.md).
