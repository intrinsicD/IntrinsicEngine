# Active Architecture Notes

Use this file as a quick “what does what, and where should it live?” reference.

> **Source-tree note.** The repository builds a single source tree under `src/`, with retired-but-still-referenced subsystems isolated in `src/legacy/`. Routing rules below describe where new work belongs in the promoted layout. See `AGENTS.md` for the authoritative source-tree map.

## Engine

- **Role:** composition root and application frame-loop owner.
- **Owns:** the top-level runtime subsystems and their wiring.
- **Does:** startup/shutdown, frame orchestration, and cross-subsystem coordination.
- **Should not own:** feature logic, scene content, or render/asset internals.

### Put work here when

- it changes app lifetime or main-loop flow;
- it wires subsystems together;
- it needs direct access to owned runtime services.

## Scene

- **Role:** runtime world state.
- **Contains:** entities, components, and scene-local systems/data.
- **Entities:** identifiers that carry component sets.
- **Components:** plain data attached to entities.
- **Scene events:** local notifications for scene changes.

### Put work here when

- it is per-entity state;
- it belongs to gameplay/editor content;
- it needs ECS queries or component updates.

## Systems

- **Role:** plain functions or function-like units that operate on scene data.
- **Active system:** runs on matching entities and reacts to registered events.
- **Inactive system:** exists in the scene but does not execute or listen.
- **Events:** systems can subscribe through the scene’s `entt::dispatcher`.
- **State:** keep long-lived state in components or owning subsystems, not inside systems.

### Use systems for

- per-frame updates;
- event reactions;
- entity/component processing;
- scene-scoped validation or sync.

### Do not use systems for

- ownership of GPU/device resources;
- asset database policy;
- UI state management;
- broad application orchestration.

## Modules

- **Meaning:** C++23 named modules, not “feature folders.”
- **Rule:** one module for one responsibility boundary.
- **Interface:** `.cppm`
- **Implementation:** `.cpp`
- **Layering:** keep dependencies one-way and prefer the smallest useful import.

### Practical placement

In `src/` (promoted layout):

- `core/` (`Extrinsic.Core.*`) — utilities, memory, tasks, telemetry, logging, filesystem, config, handles, error types.
- `assets/` (`Extrinsic.Asset.*`) — registry, payload store, load pipeline, event bus, path index.
- `ecs/` (`Extrinsic.ECS.*`) — scene registry, scene handles, components, systems.
- `geometry/` — algorithms and data structures for meshes, graphs, point clouds.
- `graphics/` (`Extrinsic.Graphics.*`, `Extrinsic.RHI.*`, `Extrinsic.Backends.*`) — rendering, RHI, Vulkan backend.
- `platform/` (`Extrinsic.Platform.*`) — window and input abstractions plus per-platform implementations.
- `runtime/` (`Extrinsic.Runtime.*`) — composition root that wires the engine together.
- `app/` — sandbox/editor entry points.

`src/legacy/` retains transitional subsystems pending retirement; follow `tasks/backlog/` and `docs/migration/legacy-retirement.md` for retirement order.

## GUI

- **Role:** presentation and user input surface.
- **Does:** render widgets, collect user choices, and emit parameter changes or commands.
- **Should not do:** core simulation, render ownership, asset ownership, or scene mutation logic by itself.

### Put work here when

- the user edits a parameter struct;
- a button, toggle, or panel should trigger an action;
- you need to expose current state for inspection.

## Quick routing rules

- **Scene state?** put it in components.
- **Cross-system notification?** use `entt::dispatcher`.
- **Per-entity dirty tracking?** use tags/components.
- **Cleanup on removal?** use `on_destroy` hooks.
- **Main-loop / subsystem wiring?** put it in `Engine` or `Runtime`.
- **User-facing controls?** put them in `GUI`, then pass commands/events to the owning system.
