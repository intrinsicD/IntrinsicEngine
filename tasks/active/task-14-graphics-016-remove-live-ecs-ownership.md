# Task 14 — Add hard implementation gate: remove live ECS ownership from promoted graphics

- Status: planned (queued for Codex) — first actual code-cleanup task; run only after Tasks 1–8 are complete.
- Owner: TBD
- Branch / PR: TBD
- Next verification step: `cmake --preset ci && cmake --build --preset ci --target IntrinsicTests && ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` after the API-boundary patch.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Start GRAPHICS-016 by removing live ECS ownership from promoted graphics APIs. Keep the patch small and do not implement new renderer features.

## Goal

Make promoted graphics consume snapshot/extraction data instead of direct `entt::registry` access.

## Problem

The current graphics renderer target links against `ExtrinsicECS` and several graphics systems accept `entt::registry&`. This conflicts with `AGENTS.md` and GRAPHICS-016.

## Required changes

1. Audit `src/graphics/renderer` for:
   - `#include <entt/...>`
   - `entt::registry&`
   - imports or target links to `ExtrinsicECS`
   - graphics components storing GPU handles/leases as ECS component state.

2. Create a minimal runtime-owned extraction-side type or adapter if needed.

   Suggested direction:
   - runtime owns ECS query and sidecar mappings;
   - graphics systems receive typed spans/packets/records;
   - do not store graphics GPU handles in canonical `src/ecs` components.

3. First patch should target API boundaries only:
   - `TransformSyncSystem`
   - `LightSystem`
   - `VisualizationSyncSystem`
   - renderer CMake dependency
   - any obvious README/doc updates

4. If full removal is too large:
   - Create a staged compatibility shim under runtime or a clearly named temporary migration file.
   - Document the temporary exception in an active/backlog task with a removal follow-up.
   - Do not leave an undocumented exception.

5. Tests:
   - Add or update `contract;graphics` tests proving graphics headers/modules do not require ECS.
   - Add or update `integration;runtime;graphics` tests proving runtime can still extract and feed graphics through the new seam.
   - Keep CPU-only tests independent of Vulkan.

## Docs

- Update GRAPHICS-016 with the implemented staging decision.
- Update `docs/architecture/graphics.md` if the snapshot/extraction API becomes more concrete.
- Update `src/graphics/renderer/README.md` if public ownership changes.

## Acceptance criteria

- Promoted graphics public APIs no longer require live `entt::registry` access, or any remaining access is explicitly marked as temporary with a removal task.
- `ExtrinsicGraphics` should not publicly link `ExtrinsicECS` unless a documented temporary exception remains.
- Runtime owns extraction/wiring.
- Tests cover the new boundary.

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
