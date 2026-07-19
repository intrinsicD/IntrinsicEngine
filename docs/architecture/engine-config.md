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
fail-closed. Application-owned values use registered records under
`app.sections`; Core treats each payload as canonical opaque JSON and never
imports the application DTO or field vocabulary.

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
  "app": {
    "sections": [
      {
        "name": "sandbox.progressive_poisson",
        "schema": "intrinsic.runtime.sandbox.progressive-poisson",
        "version": 1,
        "payload": {
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
      },
      {
        "name": "sandbox.parameterization",
        "schema": "intrinsic.runtime.sandbox.parameterization",
        "version": 1,
        "payload": {
          "strategy": "lscm",
          "view": {
            "render_mode": "cpu_layout",
            "background_mode": "grid",
            "show_distortion_heatmap": false
          },
          "lscm": {
            "auto_pins": true,
            "pin_vertex_0": 0,
            "pin_vertex_1": 1,
            "pin_uv_0": [0.0, 0.0],
            "pin_uv_1": [1.0, 0.0],
            "solver_tolerance": 1e-8,
            "max_solver_iterations": 5000
          },
          "harmonic": {
            "boundary": "circle",
            "arc_length_spacing": true,
            "clamp_non_convex_weights": true,
            "pinned_vertices": [],
            "pinned_uvs": []
          },
          "bff": {
            "mode": "automatic_conformal",
            "boundary_data": [],
            "angle_sum_tolerance": 1e-8,
            "degeneracy_tolerance": 1e-12
          }
        }
      }
    ]
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
| `app.sections[]` | `name` | Non-empty registered stable name; duplicate and unregistered names retain the registered reference default |
| `app.sections[]` | `schema`, `version` | Exact values declared by that registration |
| `app.sections[]` | `payload` | Object validated and canonicalized by that registration |

The current Sandbox registrations validate the following payload fields. The
table abbreviates `app.sections[name=sandbox.progressive_poisson].payload` as
`poisson` and `app.sections[name=sandbox.parameterization].payload` as
`parameterization`.

| Payload | Field | Values |
|---|---|---|
| `poisson` | `dimension` | Integer in `[2, 3]` |
| `poisson` | `grid_width` | Integer in `[1, 4096]` |
| `poisson` | `max_levels` | Integer in `[1, 32]` |
| `poisson` | `hash_load_factor` | Number in `[0.01, 16.0]` |
| `poisson` | `radius_alpha` | Number in `[-1.0, 0.999]`; negative keeps method defaulting semantics |
| `poisson` | `randomize_grid_origin`, `shuffle_within_levels`, `mesh_surface_interpolate_normals`, `auto_run_on_edit` | Boolean |
| `poisson` | `grid_origin_seed`, `shuffle_seed`, `mesh_surface_seed` | Integer in `[0, 2147483647]` |
| `poisson` | `prefix_count` | Integer in `[0, 10000000]`; `0` means all accepted points |
| `poisson` | `channel` | `Level`, `Phase`, `SplatRadius`, `PrefixVisible` |
| `poisson` | `backend` | `CpuReference`, `VulkanCompute`; unavailable Vulkan execution reports explicit CPU fallback, while METHOD-014 owns Operational dispatch/parity closure |
| `poisson` | `mesh_surface_sample_count` | Integer in `[1, 10000000]` |
| `poisson` | `mesh_surface_min_triangle_area` | Positive finite number in `[1e-30, 1e30]` |
| `poisson` | `debounce_seconds` | Number in `[0.0, 10.0]` |
| `parameterization` | `strategy` | `lscm`, `harmonic_cotangent`, `tutte_uniform`, `bff` |
| `parameterization.view` | `render_mode` | `cpu_layout`, `gpu_shaded`; the GPU request falls back to the CPU layout until a matching completed target is ready |
| `parameterization.view` | `background_mode` | `grid`, `checker`, `texel_density`, `texture` |
| `parameterization.view` | `show_distortion_heatmap` | Boolean; requests the canonical per-face conformal-distortion shading on the GPU path |
| `parameterization.lscm` | `auto_pins` | Boolean; when true, the geometry solver chooses its deterministic pins |
| `parameterization.lscm` | `pin_vertex_0`, `pin_vertex_1` | Distinct unsigned 32-bit vertex indices used when `auto_pins` is false |
| `parameterization.lscm` | `pin_uv_0`, `pin_uv_1` | Arrays of exactly two finite, float-representable numbers |
| `parameterization.lscm` | `solver_tolerance` | Positive finite number no greater than `1e30` |
| `parameterization.lscm` | `max_solver_iterations` | Integer in `[1, 4294967295]` |
| `parameterization.harmonic` | `boundary` | `circle`, `square`, `custom` |
| `parameterization.harmonic` | `arc_length_spacing`, `clamp_non_convex_weights` | Boolean |
| `parameterization.harmonic` | `pinned_vertices` | Array of unsigned 32-bit vertex indices; must be provided together with `pinned_uvs` |
| `parameterization.harmonic` | `pinned_uvs` | Array of two-number finite, float-representable UV arrays; must be provided together with and match the cardinality of `pinned_vertices` |
| `parameterization.bff` | `mode` | `automatic_conformal`, `target_lengths`, `target_angles` |
| `parameterization.bff` | `boundary_data` | Finite number array interpreted as positive per-boundary-edge lengths or per-boundary-vertex exterior angles by `mode`; empty for `automatic_conformal`, non-empty for target modes, and target angles must sum to `2*pi` within `angle_sum_tolerance` |
| `parameterization.bff` | `angle_sum_tolerance`, `degeneracy_tolerance` | Positive finite numbers no greater than `1e30` |

Each application section is additive within engine schema version 1 and carries
its own schema id/version. A document may omit a registered record and retain
the caller-provided reference default. Unknown, duplicate, mismatched, or
invalid records are diagnosed and do not replace that default. The registry is
a deterministic name-sorted vector of plain descriptors: a canonical default
record, a validator, and an optional non-failing post-commit callback.

The Sandbox parameterization payload round-trips all four strategy selections,
the UV-view controls, and the three typed parameter records so file/agent
callers and the downstream UI can change them without inventing an untyped
parameter bag. The serializer persists the lowercase tokens above, never a
`std::variant` alternative index.
`view.render_mode = gpu_shaded` selects an optional presentation path; it does
not change the parameterization solver backend. There is no optimized/GPU
solver selector while every implemented strategy is CPU-only.

### One-time Sandbox migration

Files written before `CORE-009` placed the two payloads directly at
`sandbox.progressive_poisson` and `sandbox.parameterization`. Move those payload
objects into the two `app.sections` records shown above and add each record's
name, schema, and version. Core intentionally has no legacy Sandbox parser: an
old root-level `sandbox` field is diagnosed as unknown and the registered
defaults remain authoritative.

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
all registered `app.sections` records.
The app-composed
`Runtime::EngineConfigControl::ApplyEngineConfigHotSubset` service previews a
candidate document against the borrowed live Engine-owned config, rejects any
difference in the boot-only fields above, and then applies only those live
fields. A non-empty recipe path is loaded and activated through the same validated
`RenderRecipeConfig` path used by startup and the editor; invalid recipe files
reject the hot apply without disturbing the currently active recipe override. An
empty path clears the active override and returns to the derived default frame
recipe. The apply result reports lexically ordered names in
`ChangedSectionNames`; `SectionChanged(name)` is the convenience query. After
the entire apply has committed, each changed registered section callback fires
exactly once. Preview, no-change, invalid, and boot-only-rejected candidates
fire none. The Sandbox progressive-Poisson payload remains value-only
method/playground state. The parameterization payload similarly holds the
selected CPU strategy, typed values, and UV-view choices. Runtime and the
Sandbox panel decode those payloads through
`Extrinsic.Runtime.SandboxConfigSections`; Core imports no Sandbox, runtime,
graphics, or method types. See
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

Sandbox creates `Extrinsic.Sandbox.ConfigSections` before boot resolution.
`ResolveEngineConfigForBoot(args, registry)` starts with
`CreateReferenceEngineConfig(registry)` so every registered default is present,
then checks, in order:

1. command line: `--engine-config <path>` or `--engine-config=<path>`;
2. environment: `INTRINSIC_ENGINE_CONFIG`;
3. default path, if it exists: `config/engine.json`.

Usable load results replace the reference config with the preview config. Missing
or invalid explicit paths preserve the reference config and keep the load
diagnostics in the boot result. The sandbox app calls this resolver before
constructing `Engine`: it first constructs the app-composed
`EngineConfigControl`, resolves through that control's owned registry, and then
moves the same control object into `Engine::AddModule(...)`. The overloads
without a registry remain available to hosts that
have no application sections.
