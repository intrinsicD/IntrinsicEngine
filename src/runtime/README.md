# Runtime

`src/runtime` composes the engine's lower layers. It owns application-selected
modules, startup and shutdown, frame phases, scene documents, editor operations,
and the transfer of CPU scene state into rendering and physics. Lower layers do
not import Runtime; graphics consumes extracted snapshots rather than live ECS.

Start with [Runtime architecture](../../docs/architecture/runtime.md), the
[layering rules](../../docs/architecture/layering.md), and the
[module inventory](../../docs/api/generated/module_inventory.md).

## Source layout

| Directory | Responsibility |
| --- | --- |
| `Kernel/` | Engine lifecycle, module composition, worlds, jobs, commands/events, services and input actions. |
| `Scene/` | Scene documents, serialization, selection and reference-scene composition. |
| `AssetWorkflow/` | Import, CPU materialization, residency handoff and asset workflow policy. |
| `Config/` | Boot/live configuration, shared codecs and field metadata. |
| `Editor/` | Editor services, workspace models, UI hosts and processing operations; private shared details live in `Editor/internal/`. |
| `GeometryIntegration/` | Geometry availability, presentation, topology, refinement and vertex-channel bindings. |
| `Rendering/` | Scene extraction, render-world pooling, snapshots, visualization, recipes, artifact publication and view capture. |
| `Modules/` | Feature configuration and runtime modules, including the ECS–physics bridge. |
| `Cameras/`, `Gizmos/`, `ImGui/`, `Visualization/` | Camera/input adapters, manipulation, editor overlay handoff and visualization recipes. |
| `Agent/` | The local agent protocol, server and command routing. |

These directories group responsibilities within the Runtime target. C++ module
names and public interfaces identify API ownership.

## Principal workflows

- **Lifecycle and frames:** the application chooses modules, services resolve
  before startup completes, and the engine runs named frame phases. Shutdown
  stops submissions and drains jobs before borrowed services are destroyed.
  See [composition and frame ownership](../../docs/architecture/runtime.md#initialization-and-frame-order).
- **Scene documents:** `SceneDocumentModule` coordinates replacement with its
  participants and owns load/save commands. The current scene format preserves
  durable entity IDs, hierarchy order, authored properties and channel bindings;
  reference-scene entities also have durable IDs. See the
  [scene replacement and persistence contract](../../docs/architecture/runtime.md#scene-replacement-lifecycle).
- **Editor processing:** operations capture inputs, perform work through the
  shared job lane, validate before publication, and record reversible changes in
  editor history. Read the [feature boundaries](../../docs/architecture/sandbox-editor-feature-boundaries.md),
  [property coherence rules](../../docs/architecture/property-coherence.md), and
  [job lifecycle](../../docs/architecture/task-graphs.md).
- **Geometry and rendering:** Runtime resolves typed properties and authored
  presentation into graphics views. Entity consumers reuse
  [spatial indices](../../docs/architecture/spatial-indices.md); the
  [consumer inventory](../../docs/architecture/spatial-index-consumers.md) names
  each admission and lifetime contract. Render-world slots are serialized on
  their owner thread; pipeline mode does not make the pool thread-safe.
- **Physics:** Runtime translates ECS authoring into the simulation world and
  writes dynamic poses back through the transform hierarchy with the standard
  dirty/update tags. Simulation state and collision algorithms remain in Physics. See the
  [physics integration contract](../../docs/architecture/physics.md).
- **Configuration and control:** file, UI and agent changes use the same
  validate-then-apply configuration path. See
  [engine configuration](../../docs/architecture/engine-config.md),
  [runtime config control](../../docs/architecture/runtime-config-control.md), and
  [the agent control lane](../../docs/architecture/agent-control-lane.md).

## Feature contracts

Selection and input ownership are documented in
[primitive selection](../../docs/architecture/primitive-selection.md) and the
[camera, picking and gizmo handoff](../../docs/adr/0006-camera-picking-and-gizmo-runtime-handoff.md).
Point-method owners document [normal estimation](../../docs/architecture/normal-estimation.md),
[registration](../../docs/architecture/registration.md),
[outlier analysis](../../docs/architecture/outlier-analysis.md),
[kernel density](../../docs/architecture/kernel-density.md),
[point spacing](../../docs/architecture/point-spacing.md),
[density weights](../../docs/architecture/density-weights.md),
[bilateral filtering](../../docs/architecture/bilateral-point-filter.md),
[keypoints](../../docs/architecture/keypoint-analysis.md),
[descriptors](../../docs/architecture/descriptor-analysis.md), and
[point construction](../../docs/architecture/point-construction.md).
Public module interfaces and those owner documents carry detailed contracts;
completed task history remains under `tasks/done/` and in Git.
