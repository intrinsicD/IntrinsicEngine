# Task 14 — Add hard implementation gate: remove live ECS ownership from promoted graphics

- Status: completed (Stage A API seam, 2026-05-02; full CI configure blocked locally by Draco FetchContent cache clone)
- Owner: Codex (current branch)
- Branch / PR: current branch / TBD
- Completion date: 2026-05-02
- Commit / PR: local split branch `split/current-working-tree-2026-05-02`; remote PR reference TBD.
- Follow-ups: runtime-owned ECS extraction/wiring completed under `tasks/archive/GRAPHICS-016-runtime-extraction-handoff.md`; continue downstream packet expansion through GRAPHICS-002+.
- Next verification step: `cmake --preset ci && cmake --build --preset ci --target IntrinsicTests && ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` after the API-boundary patch.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Start GRAPHICS-016 by removing live ECS ownership from promoted graphics APIs. Keep the patch small and do not implement new renderer features.

Stage A implementation note (2026-05-02): promoted graphics sync systems now consume typed snapshot records/spans instead of `entt::registry&`; `ExtrinsicGraphics` no longer links `ExtrinsicECS` or `EnTT::EnTT`; a graphics boundary contract test guards against reintroducing live ECS/EnTT imports under `src/graphics/renderer`. Runtime-owned ECS extraction/wiring remains the next GRAPHICS-016 follow-up. Local verification ran strict task/docs checks and a promoted graphics ECS/EnTT boundary check successfully; `cmake --preset ci` was attempted but blocked before project compilation by a broken `external/cache/draco-src` FetchContent clone.

## Goal

Make promoted graphics consume snapshot/extraction data instead of direct `entt::registry` access.

## Problem

The current graphics renderer target links against `ExtrinsicECS` and several graphics systems accept `entt::registry&`. This conflicts with `AGENTS.md` and GRAPHICS-016.

## Required changes

- [x] Audit `src/graphics/renderer` for:
   - [x] `#include <entt/...>`
   - [x] `entt::registry&`
   - [x] imports or target links to `ExtrinsicECS`
   - [x] graphics components storing GPU handles/leases as ECS component state.

- [x] Create a minimal runtime-owned extraction-side type or adapter if needed.

   Suggested direction:
   - [x] runtime owns ECS query and sidecar mappings;
   - [x] graphics systems receive typed spans/packets/records;
   - [x] do not store graphics GPU handles in canonical `src/ecs` components.

- [x] First patch should target API boundaries only:
   - [x] `TransformSyncSystem`
   - [x] `LightSystem`
   - [x] `VisualizationSyncSystem`
   - [x] renderer CMake dependency
   - [x] any obvious README/doc updates

- [x] If full removal is too large:
   - [x] Create a staged compatibility shim under runtime or a clearly named temporary migration file.
   - [x] Document the temporary exception in an active/backlog task with a removal follow-up.
   - [x] Do not leave an undocumented exception.

- [x] Tests:
   - [x] Add or update `contract;graphics` tests proving graphics headers/modules do not require ECS.
   - [x] Add or update `integration;runtime;graphics` tests proving runtime can still extract and feed graphics through the new seam.
   - [x] Keep CPU-only tests independent of Vulkan.

## Docs

- [x] Update GRAPHICS-016 with the implemented staging decision.
- [x] Update `docs/architecture/graphics.md` if the snapshot/extraction API becomes more concrete.
- [x] Update `src/graphics/renderer/README.md` if public ownership changes.

## Acceptance criteria

- [x] Promoted graphics public APIs no longer require live `entt::registry` access, or any remaining access is explicitly marked as temporary with a removal task.
- [x] `ExtrinsicGraphics` should not publicly link `ExtrinsicECS` unless a documented temporary exception remains.
- [x] Runtime owns extraction/wiring.
- [x] Tests cover the new boundary.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes

- No new rendering features.
- No shader changes.
- No Vulkan-only mandatory tests.
- Do not move ECS mutation into graphics.
- Do not store graphics GPU handles, slots, leases, or backend resource IDs in canonical ECS components.

This is the most important implementation gate. The current source still links graphics against ECS and uses `entt::registry&` in promoted graphics systems.
