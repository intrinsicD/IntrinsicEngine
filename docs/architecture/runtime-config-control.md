# Runtime Config Control

`Extrinsic.Runtime.EngineConfigControl` owns the live config-control facade for
non-ImGui callers and the Sandbox Editor. `Engine` constructs the subsystem,
keeps ownership of the active `EngineConfig` value, and exposes the facade
through `Engine::GetConfigControl()`. The facade is a set of typed subsystem
methods and DTOs, not a command bus.

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
- `sandbox.progressive_poisson`
- `sandbox.parameterization`

Applying a non-empty path first loads and validates the referenced render recipe
file. Only a usable recipe result reaches
`EngineConfigControl::ApplyRenderRecipeConfigPreview`, so an invalid file
rejects the engine-config hot apply without clearing a previously active recipe
override. Applying an empty path clears the active override and returns to the
derived default frame recipe.

The sandbox progressive-Poisson block carries the interactive playground's
reference-sampler knobs, prefix/color-channel selection, mesh surface-sampling
input controls, and auto-run debounce settings. Applying it updates only the
active value-type `EngineConfig`; the Sandbox Editor consumes that state and
drives METHOD-012 through its runtime command surface. The block has no renderer
side effects and no direct RHI/device traffic.

The sandbox parameterization block carries one of the implemented CPU strategy
tokens (`lscm`, `harmonic_cotangent`, `tutte_uniform`, or `bff`) and the typed
LSCM, harmonic, and BFF values described in
[engine config](engine-config.md). Harmonic boundary policy uses `circle`,
`square`, or `custom`; BFF boundary mode uses `automatic_conformal`,
`target_lengths`, or `target_angles`. Applying the block updates only the active
value-type `EngineConfig` and sets
`RuntimeEngineConfigApplyResult::SandboxParameterizationChanged` when its value
changed. The configured sandbox parameterization command reads this same live
state; there is no second UI-only parameter path and no optimized/GPU backend
selector.

Every other engine-config difference is boot-only and is reported in
`RuntimeEngineConfigApplyResult::RejectedBootOnlyFields`; the live config remains
unchanged.

## UI And Agent Parity

`Extrinsic.Sandbox.Editor.Shell` keeps the ImGui widget and draft-buffer
state. Its `Runtime::SandboxEditorSession` prepares an attachment-guarded
`SandboxEditorContext` through
`Extrinsic.Runtime.SandboxEditorFacades`; the shell's preview and
activation handlers call the same facade callbacks carried by that context:

- preview routes to
  `Engine::GetConfigControl().PreviewRenderRecipeConfigDocument`;
- activation routes to
  `Engine::GetConfigControl().ApplyRenderRecipeConfigPreview` with
  `RuntimeRenderRecipeActivationSource::Editor`.
- progressive-Poisson knob edits route to
  `Engine::GetConfigControl().PreviewEngineConfigControlDocument` and
  `Engine::GetConfigControl().ApplyEngineConfigHotSubset` with
  `RuntimeConfigControlSource::Editor`.
- The parameterization panel delivered by retired `UI-036` routes strategy and
  value edits through those same engine-config preview/apply methods with
  `RuntimeConfigControlSource::Editor`; the configured parameterization facade
  consumes the resulting live config.

Agent/CLI callers use the same `EngineConfigControl` methods with
`RuntimeConfigControlSource::AgentCli` or
`RuntimeRenderRecipeActivationSource::AgentCli`. The facade is CPU/headless and
does not require any ImGui frame.

## Parameterization Editor Facade

`Extrinsic.Runtime.SandboxEditorFacades` exposes the CPU parameterization
operation through the existing editor/session context. The direct command
accepts a typed `Core::Config::ParameterizationConfig` and fails closed on
invalid values; the configured command reads
`EngineConfig.sandbox.parameterization`, so editor, agent/CLI, and
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
triples, finite UV bounds, and aggregate last-result diagnostics. It carries no
raw mesh/property pointers, chart or seam records, or invented per-face
distortion values. Retired `UI-036` turns this model into the visible split UV
view and delivered the `Operational` proof; the retired runtime/config slice
closed at `CPUContracted` under the Null/default runtime contracts.
