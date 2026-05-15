# HARDEN-063 — Define promoted ECS event and command seams

## Status

- Status: in-progress (slice 1 in flight).
- Owner/agent: Claude on `claude/setup-agentic-workflow-IdWgV`.
- Branch: `claude/setup-agentic-workflow-IdWgV`.
- Started: 2026-05-15.
- Current slice: slice 1 — event payload promotion.
- Next verification step: run the default CPU gate against
  `IntrinsicECSTests` + the new `Test.ECS.Events` cases and the ECS
  contract layering test; regenerate the module inventory; promote
  remaining command-seam work (slice 2) once slice 1 lands.

## Slice plan

- **Slice 1 (this slice): event payload promotion.**
  - Inventory the six legacy events under
    `src/legacy/ECS/Components/ECS.Components.Events.cppm` and classify
    each (promote/move/retire). See "Event ownership decision" below.
  - Add the promoted `Extrinsic.ECS.Events` module under
    `src/ecs/Events/` with `SelectionChanged`, `HoverChanged`,
    `EntitySpawned`, and `GeometryModified` payload types. CPU-only,
    no graphics/runtime/asset/RHI imports.
  - Add focused `unit;ecs` payload-construction tests in
    `tests/unit/ecs/Test.ECS.Events.cpp` and wire them through
    `tests/CMakeLists.txt`.
  - Update `src/ecs/README.md`, add `src/ecs/Events/README.md`, and
    update `docs/migration/nonlegacy-parity-matrix.md` to record the
    ownership decision.
  - Regenerate `docs/api/generated/module_inventory.md`.
  - Defer command seams (lifecycle/hierarchy/transform/selection
    mutation ownership) to slice 2; record the deferral here.

- **Slice 2 (deferred): command seam decisions and promoted invariants.**
  - Decide whether entity mutation commands live in ECS as pure
    data/operations or in runtime/editor as composition behavior.
  - Define lifecycle command policy: create, destroy, recursive
    destroy or orphan handling, default bootstrap, attach/detach,
    transform mutation. Existing helpers
    (`Scene::CreateDefault`/`EmplaceDefaults`, `Hierarchy::Attach`/
    `Detach`, `entt::registry` mutation through `Registry::Raw()`)
    are the baseline — slice 2 either documents them as the command
    seam or records an explicit alternative.
  - Define selection command ownership: replace/add/toggle/clear
    selection semantics and primitive-selection caches; keep input
    interpretation and GPU pick readback outside ECS.
  - Add focused `unit;ecs` tests for any promoted command invariants
    (`Test.ECS.Commands.cpp`, `Test.ECS.SelectionCommands.cpp` as
    appropriate).
  - Update `src/ecs/README.md` and the parity matrix with the command
    decisions.

## Event ownership decision

| Legacy event | Decision | Promoted payload | Rationale |
|---|---|---|---|
| `SelectionChanged` | Promote to ECS | `Extrinsic::ECS::Events::SelectionChanged` | Entity-keyed scene-side notification; selection tag membership is ECS-owned. |
| `HoverChanged` | Promote to ECS | `Extrinsic::ECS::Events::HoverChanged` | Entity-keyed scene-side notification; hover tag is ECS-owned. |
| `EntitySpawned` | Promote to ECS | `Extrinsic::ECS::Events::EntitySpawned` | Entity lifecycle event; scene-side and CPU-only. |
| `GeometryModified` | Promote to ECS | `Extrinsic::ECS::Events::GeometryModified` | CPU geometry mutation notification; complements `DirtyTags::*` per-domain stamps with a coarse signal. |
| `GpuPickCompleted` | Runtime/graphics-owned (not promoted to ECS) | — | GPU pick readback is graphics-owned (`HARDEN-063` Context: "keep input interpretation and GPU pick readback outside ECS"). Runtime/editor translates a completed pick into an ECS selection mutation, which fires the promoted `SelectionChanged`. |
| `GeometryUploadFailed` | Runtime/graphics-owned (not promoted to ECS) | — | GPU upload failure is a graphics-layer concern; ECS learns about failure only through asset events or runtime diagnostics. |

## Goal
- Promote or formally retire legacy ECS event/command seams needed by runtime/editor workflows without coupling ECS to higher layers.

## Non-goals
- No UI/editor implementation.
- No scene serialization or asset ingest work.
- No physics event bus or collision-event implementation; those require `ARCH-001` first.
- No GPU pick readback, GPU upload failure plumbing, or input event interpretation in `src/ecs`.

## Context
- Owner/layer: `ecs`; consumers are expected in `runtime`, editor/app, or tests through explicit seams.
- Source review: [`docs/reviews/2026-05-13-src-ecs-gap-analysis.md`](../../docs/reviews/2026-05-13-src-ecs-gap-analysis.md).
- Legacy `src/legacy/ECS/Components/ECS.Components.Events.cppm` defines selection, hover, GPU pick, entity-spawned, geometry-modified, and geometry-upload-failed events.
- Promoted `src/ecs` currently has components and registry wrappers but no promoted event or command seam.
- Lower layers must not know about runtime/editor/UI behavior, but runtime composition needs stable data contracts for mutation and observation.

## Required changes
- [x] Inventory legacy ECS event types and classify each as: promote to `ecs`, move to `runtime`, move to graphics/runtime extraction, or retire (slice 1; see "Event ownership decision" table above).
- [x] Define minimal CPU-only event payload modules in `src/ecs/Events/` for the promoted events (slice 1).
- [ ] Define whether entity mutation commands belong in ECS as pure data/operations or in runtime/editor as composition behavior (slice 2).
- [ ] Include lifecycle command policy for create, destroy, recursive destroy or orphan handling, default bootstrap, attach/detach, and transform mutation (slice 2).
- [ ] Include selection command ownership decisions for replace/add/toggle/clear selection and primitive-selection caches; keep input interpretation and GPU pick readback outside ECS unless represented only as CPU event payloads (slice 2).
- [x] Add focused tests for event payload construction (slice 1).
- [ ] Add focused tests for any promoted command invariants (slice 2).
- [x] Record unpromoted legacy events as explicit non-goals or follow-up tasks (slice 1: `GpuPickCompleted` and `GeometryUploadFailed` stay runtime/graphics-owned).

## Tests
- [x] `tests/unit/ecs/Test.ECS.Events.cpp` — payload construction, default-`InvalidEntityHandle` initialization, trivially-copyable invariants for all four promoted events (slice 1).
- [ ] `tests/unit/ecs/Test.ECS.Commands.cpp` — deterministic command application and invalid-handle behavior (slice 2; only if commands are promoted).
- [ ] `tests/unit/ecs/Test.ECS.SelectionCommands.cpp` — replace/add/toggle/clear selection semantics (slice 2; only if owned by ECS).
- [x] Existing `tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp` already enforces that any new `src/ecs/Events/*` files do not import higher-layer modules (slice 1: verified by running the contract test).

## Docs
- [x] Update `src/ecs/README.md` with the event ownership decision (slice 1).
- [x] Add `src/ecs/Events/README.md` enumerating the promoted module surface and the deliberately-runtime/graphics-owned events (slice 1).
- [x] Update [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md) ECS row to reflect promoted `Extrinsic.ECS.Events` and the slice-2 deferral of command seams (slice 1).
- [x] Regenerate [`docs/api/generated/module_inventory.md`](../../docs/api/generated/module_inventory.md) (slice 1).
- [ ] Update `src/ecs/README.md` and the parity matrix again with slice-2 command decisions (slice 2).

## Acceptance criteria
- [x] Legacy ECS event surface has an explicit promoted-owner decision recorded in this task and in `docs/migration/nonlegacy-parity-matrix.md` (slice 1).
- [x] Any promoted ECS events are deterministic payload types with no graphics/runtime dependencies (slice 1).
- [ ] Promoted command decisions cover entity lifecycle, hierarchy mutation, transform mutation, and selection mutation ownership (slice 2).
- [x] Runtime/editor-specific behaviors are not placed in the ECS layer for convenience (slice 1: `GpuPickCompleted` / `GeometryUploadFailed` kept runtime/graphics-owned).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests IntrinsicEcsContractTests
ctest --test-dir build/ci -L 'ecs' --output-on-failure --timeout 60
ctest --test-dir build/ci -L 'contract' --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Adding editor/UI, graphics readback, or runtime scene-manager dependencies to `src/ecs`.
- Adding collision or rigid-body event semantics before the physics layer contract is approved.
- Deleting legacy events without a separate mechanical cleanup task.
- Promoting `GpuPickCompleted` or `GeometryUploadFailed` to ECS in slice 1 (deliberately runtime/graphics-owned).

## Next verification step
- Run the default CPU gate against `IntrinsicECSTests` + the new `Test.ECS.Events` cases and the ECS contract layering test; regenerate the module inventory; once slice 1 lands, open slice 2 for the command seam decisions.
