# Primitive selection

`SceneInteractionModule` owns active-world interaction state and publishes its
`SelectionController` as a runtime service. The controller owns entity selection
and ordered primitive sets. A separate module/service lifetime is unnecessary:
selection, outstanding picks, and gizmos share the same world and document reset
boundaries. Geometry kernels receive explicit indices or typed properties and do
not reach into runtime services.

## Using selection

Open **Mesh / Selection**, **Graph / Selection**, or **PointCloud / Selection**.
The **Pick** setting chooses entities, vertices/points, edges, or faces. Click
replaces the set on the hit entity/domain, Shift-click adds, and Ctrl-click
toggles. Both left and right modifiers work; Ctrl takes precedence. Vertex/edge
selection on a rendered surface chooses the nearest corner/edge of the hit face.
Graph nodes and point-cloud points use the same vertex/point mode. An ordinary
background click clears primitive sets while retaining the active entity;
entity mode retains the existing entity-selection behavior. ImGui capture and
gizmo capture suppress viewport picks; primitive mode disables transform gizmos.

The selection panel also exposes canonical element domains, explicit index
add/remove, select all, invert, clear, and selection order. Halfedges are available
through these domain/index controls; viewport edge picking selects undirected
edges. Sets remain separate for each entity and domain. Highlights use orange
points and edge/face outlines, including occluded selected elements, without
changing geometry properties or the active scalar/color visualization. Point
radius is in world units.

In **Mesh / Geodesics / Virtual Source Propagation**, **Use selected vertices as
sources** copies the vertex set into the geodesics draft. Compute applies that
configuration through the normal validated config path. Parameterization offers
an equivalent action for two ordered LSCM pins and for harmonic/Tutte boundary
pins. Boundary pin UVs begin with the current UV values, or zero if absent; edit
them before applying. The method validates boundary eligibility and UV constraints.
Selection changes after copying do not silently change method inputs.

## Shared API and state validity

`SelectionController::ReadPrimitives(scene, entityId, domain)` returns a copied
`PrimitiveSelectionSnapshot` with status, domain, entity, row count, and unique
indices in selection order. Removing and re-adding an index moves it to the end.
`EditPrimitives` atomically validates a batch before applying Replace, Add, Toggle,
Remove, Clear, All, or Invert; deleted and out-of-range rows are rejected.
`ReadEditorPrimitiveSelection` and `ApplyEditorPrimitiveSelection` expose the same
operations through the attachment-validated editor/agent command surface.
Methods requiring seeds, constraints, or subsets can consume this snapshot on
any compatible canonical domain. Kernels retain their own method-specific
eligibility checks; a selected face does not automatically become a vertex seed.

Topology, deletion-mask revisions, and cardinality identify the selected rows.
A topology edit expires the set rather than applying old indices to new rows.
Position and unrelated attribute edits preserve selections when row-identity
channels are present. Minimal point sources without such channels conservatively
expire selection on position changes. Expired reads contain no indices; the
interaction owner prunes expired records before rendering. Scene replacement,
world switching, entity destruction, and expired command attachments fail closed.
Selection sets are transient scene state; copying them to a method's serialized
inputs makes that method invocation reproducible. Selection edits do not enter
the geometry undo stack.

Picks capture their target and combination mode at submission. The owner retains
issuing-frame camera context, topology/position revisions, world transform,
world handle, epoch, and sequence until readback. Completed readbacks within a
maintenance batch replay in issue order. Changed geometry or transforms
reject delayed primitive picks. Hover results never mutate primitive sets.
Graphics receives copied point/line highlight packets through the existing
world-qualified extraction snapshot and transient-debug recipe path.

## Configuration and verification

`runtime.selection` is an app config section with schema
`intrinsic.runtime.selection`, version 1:

```json
{"target":"vertex","highlight":true,"point_radius":0.01}
```

Defaults are entity picking, highlights enabled, and radius 0.01. UI and
config/agent callers share preview/validate/apply. The interaction module reads
the applied section before viewport input; invalid settings cannot be applied
through the config command path. Applications composing the interaction module
can register `MakeSelectionConfigSectionRegistration()`; Sandbox does so.

CPU contract tests in `Test.PrimitiveSelection.cpp`, `Test.SceneInteractionModule.cpp`,
and `Test.SelectionSnapshotExtraction.cpp` exercise mutation, expiry, production
input/readback hooks, method consumption, and renderer snapshot publication.
These tests do not assert Vulkan pixel appearance. Box/lasso/brush selection and
arbitrary surface-point creation are outside this implementation.
