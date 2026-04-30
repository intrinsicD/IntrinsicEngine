# Source-Tree Move Plan (RORG-090)

This document defines the **pre-move, mechanical-only** execution plan for promoting the repository from the current dual source roots (`src/` + `src_new/`) into the final canonical `src/` layout.

## Scope and constraints

- This plan applies to the Phase 9 move sequence (RORG-090 through RORG-103).
- All source-tree movement must be **mechanical** (path/layout, build wiring, include-path updates) and reviewed separately from semantic feature/refactor changes.
- C++ module names remain stable throughout the move unless a dedicated follow-up task explicitly authorizes a rename.

## Current source roots

Current code roots to be reorganized:

- `src/` (legacy tree, including legacy `Geometry` today)
- `src_new/` (promoted modern layers: `Core`, `Assets`, `ECS`, `Graphics`, `Platform`, `Runtime`, `App`)

## Target source roots

Final canonical layout under `src/`:

- `src/legacy/`
- `src/core/`
- `src/assets/`
- `src/ecs/`
- `src/geometry/`
- `src/graphics/rhi/`
- `src/graphics/vulkan/`
- `src/graphics/framegraph/`
- `src/graphics/renderer/`
- `src/platform/`
- `src/runtime/`
- `src/app/`

## Exact move table

| Source path | Destination path | Planned task |
|---|---|---|
| `src/Geometry` | `src/geometry` | RORG-093 |
| `src/<everything else>` | `src/legacy/<same relative path>` | RORG-092 |
| `src_new/Core` | `src/core` | RORG-094 |
| `src_new/Assets` | `src/assets` | RORG-095 |
| `src_new/ECS` | `src/ecs` | RORG-096 |
| `src_new/Graphics/RHI` | `src/graphics/rhi` | RORG-097 |
| `src_new/Graphics/Backends/Vulkan` | `src/graphics/vulkan` | RORG-098 |
| `src_new/Graphics/<remaining modules>` | `src/graphics/framegraph`, `src/graphics/renderer`, or another explicit owning graphics directory | RORG-099 |
| `src_new/Platform` | `src/platform` | RORG-100 |
| `src_new/Runtime` | `src/runtime` | RORG-101 |
| `src_new/App` | `src/app` | RORG-102 |
| empty `src_new/` root | removed | RORG-103 |

## CMake targets that must be updated

The following build entry points are expected to require path rewiring as move tasks execute:

- Root build entry (`CMakeLists.txt`).
- Source-root wiring and layer target aggregation in `src/CMakeLists.txt` and `src_new/CMakeLists.txt`.
- Supporting CMake module files under `cmake/` that hardcode source-tree locations.
- Benchmark and test target glue where source paths are explicitly referenced.

Move tasks must keep targets buildable at every step, ideally through intermediate source-root variables introduced by RORG-091.

## Include-path and source-path updates

Expected path-sensitive updates during move tasks:

- CMake `target_sources(...)` and source globs listing old locations.
- `target_include_directories(...)` entries pointing at `src_new/*` or pre-legacy `src/*` paths.
- Tool scripts that scan hardcoded source roots (`src_new` only assumptions, legacy `src` assumptions).
- Documentation snippets and developer instructions that mention old paths.

## Module inventory generator implications

`tools/repo/generate_module_inventory.py` already supports `src/` scanning and migration use cases. During Phase 9:

- Keep generated inventory path stable (`docs/api/generated/module_inventory.md`).
- Ensure inventory classification reflects the evolving layout (`core/assets/ecs/geometry/graphics/platform/runtime/app/legacy`).
- Remove migration-only `src_new` emphasis when RORG-103 completes and `src_new/` is deleted.

## CI updates required during move window

As paths migrate, ensure CI continues to execute equivalent validation with updated locations:

- CMake configure/build/test workflows.
- Layering check (`tools/repo/check_layering.py`) allowlist adjustments only when temporary shims are documented.
- Docs link, task, method manifest, and benchmark manifest checks if file paths move.
- Any workflow step that references old tool paths or old source roots.

## Rollback plan

If a move task introduces a build break, unresolved include graph issue, or unexpected module visibility regression:

1. Revert only the failing move commit(s) for the affected task (no partial semantic edits mixed in).
2. Re-run baseline verification (`cmake --preset ci`, build, tests, structural checks).
3. Record blocker details and temporary status in a current task under `tasks/active/` with a removal task ID.
4. Split the task into smaller mechanical sub-steps (for example, move-only then path-fix-only) before retrying.

## Pre-move review gate

Before starting RORG-092 file movement, reviewers should confirm:

- Move table matches the agreed target architecture.
- Module-name stability is explicitly preserved.
- CMake abstraction strategy (RORG-091) is ready to reduce churn.
- Rollback strategy is documented and actionable.
