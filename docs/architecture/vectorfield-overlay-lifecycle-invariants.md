# VectorField + Overlay Lifecycle Invariants

This note defines the single-source invariants for the vector-field/overlay path:
`OverlayEntityFactory -> ECS tags/components -> lifecycle systems -> render extraction`.

## Invariant A — Authoritative geometry ownership

- Overlay factory is responsible for initial geometry upload contract.
- ECS component (`Surface`, `Line`, `Point`, or `VectorField`) carries authoritative handle references after factory return.
- Lifecycle systems may stage/refresh GPU views, but must not invent ownership absent source component data.

## Invariant B — Dirty-domain monotonicity

When a new overlay entity is created or its topology/attributes are replaced, relevant dirty tags must be attached before the next lifecycle pass:

- topology domain dirty (connectivity changed)
- attribute domain dirty (positions/colors/radii/etc. changed)

No lifecycle pass should assume clean state after replacement without explicit clear by sync system.

## Invariant C — Parent/child destruction closure

If a parent entity owns vector-field child overlays, parent destruction must synchronously remove/detach all dependent overlays in the same ECS maintenance phase.

This avoids stale GPU slot references and prevents extraction from seeing orphaned children.

## Invariant D — Selection/outline parity

Selection-outline stencil eligibility for overlays must match the active overlay primitive set:

- line overlays
- point overlays
- sphere-point overlays (if represented as point class variant)

No overlay subtype should silently bypass outline classification.

## Invariant E — Extraction determinism

Given the same ECS snapshot, extraction emits identical overlay draw packets independent of entity iteration order.

This requires stable per-entity ordering keys and no hidden mutable global filtering state.

## Required tests

- Contract tests for capabilities/domain exposure in `tests/Test_EditorUI.cpp`.
- Lifecycle tests for create/destroy cleanup paths (`tests/Test_*Lifecycle*.cpp`).
- Render extraction regression test for deterministic packet coverage (`tests/Test_RenderExtraction.cpp`).
