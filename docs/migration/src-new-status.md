# `src_new/` Transition Status

This status page tracks promotion of `src_new` subsystems into final `src/` canonical roots.

## Legend

- `not-started`
- `in-progress`
- `blocked`
- `done`

## Promotion board

| Subsystem | Source path | Target path | Status | Tracking task |
|---|---|---|---|---|
| Core | `src_new/Core` | `src/core` | done | RORG-094 |
| Assets | `src_new/Assets` | `src/assets` | done | RORG-095 |
| ECS | `src_new/ECS` | `src/ecs` | not-started | RORG-096 |
| Graphics RHI | `src_new/Graphics/RHI` | `src/graphics/rhi` | not-started | RORG-097 |
| Graphics Vulkan backend | `src_new/Graphics/Backends/Vulkan` | `src/graphics/vulkan` | not-started | RORG-098 |
| Remaining graphics modules | `src_new/Graphics/*` | `src/graphics/*` | not-started | RORG-099 |
| Platform | `src_new/Platform` | `src/platform` | not-started | RORG-100 |
| Runtime | `src_new/Runtime` | `src/runtime` | not-started | RORG-101 |
| App | `src_new/App` | `src/app` | not-started | RORG-102 |

## Completion gate for deleting `src_new/`

`src_new/` should only be removed after all promotion rows are `done`, references are updated, and validation passes for build/tests/docs/tooling (tracked by RORG-103).
