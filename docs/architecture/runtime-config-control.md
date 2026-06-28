# Runtime Config Control

`Extrinsic.Runtime.Engine` owns the live config-control facade for non-ImGui
callers and the Sandbox Editor. The facade is a set of typed Engine methods and
DTOs, not a command bus.

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

The only live engine-config field in the current subset is
`render.default_recipe_config_path`.

Applying a non-empty path first loads and validates the referenced render recipe
file. Only a usable recipe result reaches `ApplyRenderRecipeConfigPreview`, so an
invalid file rejects the engine-config hot apply without clearing a previously
active recipe override. Applying an empty path clears the active override and
returns to the derived default frame recipe.

Every other engine-config difference is boot-only and is reported in
`RuntimeEngineConfigApplyResult::RejectedBootOnlyFields`; the live config remains
unchanged.

## UI And Agent Parity

`SandboxEditorUi` keeps widget/draft-buffer state only. Its preview and
activation commands call the same facade callbacks supplied by
`SandboxEditorUi::Attach(Engine&)`:

- preview routes to `Engine::PreviewRenderRecipeConfigDocument`;
- activation routes to `Engine::ApplyRenderRecipeConfigPreview` with
  `RuntimeRenderRecipeActivationSource::Editor`.

Agent/CLI callers use the same Engine methods with
`RuntimeConfigControlSource::AgentCli` or
`RuntimeRenderRecipeActivationSource::AgentCli`. The facade is CPU/headless and
does not require any ImGui frame.
