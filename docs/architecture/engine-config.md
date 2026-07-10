# Engine Config File

`Extrinsic.Core.Config.EngineLoad` defines the side-effect-free JSON file lane
for `Core::Config::EngineConfig`.

## Schema

Engine config files are JSON objects with:

- `schema`: `"intrinsic.core.engine-config"`
- `version`: `1`

All parsed values start from a caller-provided reference `EngineConfig`. Missing
fields retain those reference defaults. Unknown fields and invalid values are
diagnosed and ignored, so the resulting preview remains deterministic and
fail-closed.

```json
{
  "schema": "intrinsic.core.engine-config",
  "version": 1,
  "window": {
    "title": "Modular Vulkan Engine",
    "width": 1600,
    "height": 900,
    "resizable": true,
    "backend": "Configured"
  },
  "render": {
    "backend": "Vulkan",
    "enable_promoted_vulkan_device": true,
    "enable_validation": true,
    "enable_vsync": true,
    "frames_in_flight": 2,
    "default_recipe_config_path": "config/render-recipe.json",
    "synchronous_extraction": true
  },
  "simulation": {
    "worker_thread_count": 0
  },
  "reference_scene": {
    "enabled": true,
    "selector": "Triangle"
  },
  "camera": {
    "enabled": true,
    "controller": "Orbit"
  },
  "sandbox": {
    "progressive_poisson": {
      "dimension": 3,
      "grid_width": 4,
      "max_levels": 16,
      "hash_load_factor": 0.25,
      "radius_alpha": -1.0,
      "randomize_grid_origin": true,
      "grid_origin_seed": 1337,
      "shuffle_within_levels": true,
      "shuffle_seed": 1374496523,
      "prefix_count": 0,
      "channel": "Level",
      "backend": "CpuReference",
      "mesh_surface_sample_count": 4096,
      "mesh_surface_seed": 1337,
      "mesh_surface_min_triangle_area": 1e-14,
      "mesh_surface_interpolate_normals": true,
      "auto_run_on_edit": true,
      "debounce_seconds": 0.25
    }
  }
}
```

## Fields

| Object | Field | Values |
|---|---|---|
| `window` | `title` | Non-empty string |
| `window` | `width`, `height` | Integer in `[1, 32768]` |
| `window` | `resizable` | Boolean |
| `window` | `backend` | `Configured`, `Null` |
| `render` | `backend` | `Vulkan` |
| `render` | `enable_promoted_vulkan_device` | Boolean |
| `render` | `enable_validation` | Boolean |
| `render` | `enable_vsync` | Boolean |
| `render` | `frames_in_flight` | Integer in `[1, 8]` |
| `render` | `default_recipe_config_path` | String path; empty disables startup/live recipe loading |
| `render` | `synchronous_extraction` | Boolean |
| `simulation` | `worker_thread_count` | Integer in `[0, 1024]`; `0` keeps scheduler auto-detect |
| `reference_scene` | `enabled` | Boolean |
| `reference_scene` | `selector` | `Triangle` |
| `camera` | `enabled` | Boolean |
| `camera` | `controller` | `Orbit`, `Fly`, `FreeLook`, `TopDown` |
| `sandbox.progressive_poisson` | `dimension` | Integer in `[2, 3]` |
| `sandbox.progressive_poisson` | `grid_width` | Integer in `[1, 4096]` |
| `sandbox.progressive_poisson` | `max_levels` | Integer in `[1, 32]` |
| `sandbox.progressive_poisson` | `hash_load_factor` | Number in `[0.01, 16.0]` |
| `sandbox.progressive_poisson` | `radius_alpha` | Number in `[-1.0, 0.999]`; negative keeps method defaulting semantics |
| `sandbox.progressive_poisson` | `randomize_grid_origin`, `shuffle_within_levels`, `mesh_surface_interpolate_normals`, `auto_run_on_edit` | Boolean |
| `sandbox.progressive_poisson` | `grid_origin_seed`, `shuffle_seed`, `mesh_surface_seed` | Integer in `[0, 2147483647]` |
| `sandbox.progressive_poisson` | `prefix_count` | Integer in `[0, 10000000]`; `0` means all accepted points |
| `sandbox.progressive_poisson` | `channel` | `Level`, `Phase`, `SplatRadius`, `PrefixVisible` |
| `sandbox.progressive_poisson` | `backend` | `CpuReference`, `VulkanCompute`; `VulkanCompute` reports CPU fallback until METHOD-013 installs operational dispatches |
| `sandbox.progressive_poisson` | `mesh_surface_sample_count` | Integer in `[1, 10000000]` |
| `sandbox.progressive_poisson` | `mesh_surface_min_triangle_area` | Positive finite number in `[1e-30, 1e30]` |
| `sandbox.progressive_poisson` | `debounce_seconds` | Number in `[0.0, 10.0]` |

## Mutability

The schema is primarily a boot config. Runtime reads it before constructing
`Engine`, and most fields remain boot-only:

- graphics backend selection and promoted-Vulkan opt-in;
- frames-in-flight and synchronous extraction mode;
- validation and VSync toggles;
- scheduler worker-thread count;
- window title, size, resizable flag, and platform backend override;
- reference-scene and initial camera-controller selection.

The current live hot-apply subset is deliberately narrow:
`render.default_recipe_config_path` and
`sandbox.progressive_poisson`.
`Runtime::Engine::GetConfigControl().ApplyEngineConfigHotSubset` previews a
candidate document against the live Engine-owned config, rejects any difference
in the boot-only fields above, and then applies only those live fields. A
non-empty recipe path is loaded and activated through the same validated
`RenderRecipeConfig` path used by startup and the editor; invalid recipe files
reject the hot apply without disturbing the currently active recipe override. An
empty path clears the active override and returns to the derived default frame
recipe. The sandbox progressive-Poisson block is value-only method/playground
state consumed by the Sandbox Editor and agent/CLI callers; it does not import
runtime from core or mutate renderer state by itself. See
[runtime config control](runtime-config-control.md).

## Diagnostics

`PreviewEngineConfig(document, defaults, options)` parses in memory and returns
an `EngineConfigLoadResult`. `LoadEngineConfigFile(path, defaults, options)`
uses the core file backend, then routes through the same preview function.

States:

- `Valid`: schema and values were accepted.
- `FallbackApplied`: at least one unknown field or invalid value was ignored and
  the reference default remained authoritative for that field.
- `Invalid`: the document could not be loaded, parsed, or matched to the schema.
- `Unsupported`: the schema version is not supported.

The preview result owns copied diagnostics and the fully-defaulted `EngineConfig`.
It does not mutate global state, create engine subsystems, touch graphics/RHI
objects, or import runtime.

## Runtime Boot Path

`Extrinsic.Runtime.Engine::ResolveEngineConfigForBoot(...)` starts with
`CreateReferenceEngineConfig()` defaults and then checks, in order:

1. command line: `--engine-config <path>` or `--engine-config=<path>`;
2. environment: `INTRINSIC_ENGINE_CONFIG`;
3. default path, if it exists: `config/engine.json`.

Usable load results replace the reference config with the preview config. Missing
or invalid explicit paths preserve the reference config and keep the load
diagnostics in the boot result. The sandbox app calls this resolver before
constructing `Engine`.
