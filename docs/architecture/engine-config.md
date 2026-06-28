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
| `render` | `synchronous_extraction` | Boolean |
| `simulation` | `worker_thread_count` | Integer in `[0, 1024]`; `0` keeps scheduler auto-detect |
| `reference_scene` | `enabled` | Boolean |
| `reference_scene` | `selector` | `Triangle` |
| `camera` | `enabled` | Boolean |
| `camera` | `controller` | `Orbit`, `Fly`, `FreeLook`, `TopDown` |

## Mutability

The current schema is a boot config. Runtime reads it before constructing
`Engine`, and all fields are treated as boot-only for `CORE-003`:

- graphics backend selection and promoted-Vulkan opt-in;
- frames-in-flight and synchronous extraction mode;
- validation and VSync toggles;
- scheduler worker-thread count;
- window title, size, resizable flag, and platform backend override;
- reference-scene and initial camera-controller selection.

There is no hot-apply subset yet. `RUNTIME-131` owns the later agent/CLI control
facade; it may preview the same schema but must not mutate boot-only fields in a
live engine unless a follow-up task defines a specific apply contract.

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
