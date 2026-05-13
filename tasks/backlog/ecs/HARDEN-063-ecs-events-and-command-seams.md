# HARDEN-063 — Define promoted ECS event and command seams

## Goal
- Promote or formally retire legacy ECS event/command seams needed by runtime/editor workflows without coupling ECS to higher layers.

## Non-goals
- No UI/editor implementation.
- No scene serialization or asset ingest work.
- No physics event bus or collision-event implementation; those require `ARCH-001` first.

## Context
- Owner/layer: `ecs`; consumers are expected in `runtime`, editor/app, or tests through explicit seams.
- Source review: [`docs/reviews/2026-05-13-src-ecs-gap-analysis.md`](../../../docs/reviews/2026-05-13-src-ecs-gap-analysis.md).
- Legacy `src/legacy/ECS/Components/ECS.Components.Events.cppm` defines selection, hover, GPU pick, entity-spawned, geometry-modified, and geometry-upload-failed events.
- Promoted `src/ecs` currently has components and registry wrappers but no promoted event or command seam.
- Lower layers must not know about runtime/editor/UI behavior, but runtime composition needs stable data contracts for mutation and observation.

## Required changes
- [ ] Inventory legacy ECS event types and classify each as: promote to `ecs`, move to `runtime`, move to graphics/runtime extraction, or retire.
- [ ] If any events are promoted, define minimal CPU-only event payload modules in `src/ecs` that do not import graphics/runtime/platform/app.
- [ ] Define whether entity mutation commands belong in ECS as pure data/operations or in runtime/editor as composition behavior.
- [ ] Include lifecycle command policy for create, destroy, recursive destroy or orphan handling, default bootstrap, attach/detach, and transform mutation.
- [ ] Include selection command ownership decisions for replace/add/toggle/clear selection and primitive-selection caches; keep input interpretation and GPU pick readback outside ECS unless represented only as CPU event payloads.
- [ ] Add focused tests for event payload construction and any promoted command invariants.
- [ ] Record unpromoted legacy events as explicit non-goals or follow-up tasks.

## Tests
- [ ] Add/update `tests/unit/ecs/Test.ECS.Events.cpp` for pure ECS events, or `tests/contract/runtime/` tests if ownership moves to runtime.
- [ ] Add/update `tests/unit/ecs/Test.ECS.Commands.cpp` for any promoted command application semantics.
- [ ] Add/update `tests/unit/ecs/Test.ECS.SelectionCommands.cpp` if selection mutation is owned by ECS.
- [ ] Add structural tests if needed to enforce that promoted ECS events remain CPU-only and layer-clean.

## Docs
- [ ] Update `src/ecs/README.md` with the event/command ownership decision.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` with factual readiness and remaining retirement blockers.

## Acceptance criteria
- [ ] Legacy ECS event surface has an explicit promoted-owner decision.
- [ ] Any promoted ECS events are deterministic payload types with no graphics/runtime dependencies.
- [ ] Promoted command decisions cover entity lifecycle, hierarchy mutation, transform mutation, and selection mutation ownership.
- [ ] Runtime/editor-specific behaviors are not placed in the ECS layer for convenience.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests
ctest --test-dir build/ci -L 'ecs' --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding editor/UI, graphics readback, or runtime scene-manager dependencies to `src/ecs`.
- Adding collision or rigid-body event semantics before the physics layer contract is approved.
- Deleting legacy events without a separate mechanical cleanup task.

