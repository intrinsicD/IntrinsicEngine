# VectorField + Overlay Lifecycle Invariants

This note defines the single-source invariants for vector-field/overlay paths.
The legacy path was `OverlayEntityFactory -> ECS tags/components -> lifecycle
systems -> render extraction`; `RUNTIME-104` retired that child-entity producer
for current promoted workflows. The promoted vector-field path is
`Runtime.VisualizationAdapters -> Graphics.VisualizationPackets -> renderer
visualization-overlay pass` and creates no child ECS entity.

## Invariant A — Authoritative geometry ownership

- Current promoted workflows keep source geometry/property ownership in
  runtime/editor/app-owned `GeometrySources` or adapter input data.
- `VectorFieldAdapter` emits immutable `VectorFieldOverlayPacket` records from
  caller-owned buffers; it does not create child `Graph` entities or store
  graphics/RHI handles in ECS.
- Lifecycle systems may stage/refresh GPU views, but must not invent ownership
  absent source component data.

## Invariant B — Dirty-domain monotonicity

When geometry topology/attributes are replaced, relevant dirty tags must be
attached before the next lifecycle pass:

- topology domain dirty (connectivity changed)
- attribute domain dirty (positions/colors/radii/etc. changed)

No lifecycle pass should assume clean state after replacement without explicit
clear by sync system. Packet-only vector-field overlays carry the current
adapter inputs for the frame rather than maintaining an independent dirty stamp.

## Invariant C — Parent/child destruction closure

If a future parent entity owns vector-field child overlays, parent destruction
must synchronously remove/detach all dependent overlays in the same ECS
maintenance phase.

This avoids stale GPU slot references and prevents extraction from seeing orphaned children.

Current promoted vector-field packets satisfy this invariant by not creating
child entities; scene replacement still drains runtime extraction sidecars
before graphics observes the next frame.

## Invariant D — Selection/outline parity

Selection-outline stencil eligibility for selectable overlays must match the
active overlay primitive set:

- line overlays
- point overlays
- sphere-point overlays (if represented as point class variant)

No overlay subtype should silently bypass outline classification. Current
visualization vector-field/isoline packets are visual-only; ordinary mesh,
graph, point-cloud, and mesh primitive-view renderables use the existing
runtime selection snapshot and graphics outline lanes.

## Invariant E — Extraction determinism

Given the same ECS snapshot and runtime adapter bindings, extraction emits
identical overlay draw packets independent of entity iteration order.

This requires stable per-entity ordering keys and no hidden mutable global
filtering state.

## Required tests

- Contract tests for capabilities/domain exposure in
  `tests/contract/runtime/Test.SandboxEditorUi.cpp`.
- Runtime extraction tests for visualization packet coverage in
  `tests/integration/runtime/Test.RuntimeRenderExtraction.cpp`.
- If a future task reintroduces child overlay entities, add lifecycle tests for
  create/destroy cleanup paths before claiming the producer is
  `CPUContracted`.
