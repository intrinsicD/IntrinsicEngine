# Runtime

`src/runtime` is the composition root for the engine. It owns
subsystem instantiation order, frame-phase orchestration, and deterministic
startup/shutdown.

## Public module surface

| Module | Responsibility |
|---|---|
| `Extrinsic.Runtime.Engine` | Composition root, frame loop, subsystem wiring, app-facing reference engine config helper |
| `Extrinsic.Runtime.FrameClock` | Per-frame wall-clock management |
| `Extrinsic.Runtime.FrameLoop` | Testable platform/render/maintenance/shutdown phase contracts |
| `Extrinsic.Runtime.RenderExtraction` | Runtime-owned ECS-to-graphics extraction cache and snapshot handoff |
| `Extrinsic.Runtime.StreamingExecutor` | Persistent background streaming task execution |

`Extrinsic.Runtime.Engine` exports `CreateReferenceEngineConfig()` so reference
applications can request the standard runtime configuration without importing
lower-layer `core` config modules directly. Applications may pass the returned
config to `Engine`; runtime remains responsible for interpreting subsystem
configuration and composition.

## Canonical frame loop phases (`Engine::RunFrame`)

1. Platform events / resize handling.
2. Fixed-step simulation.
3. Variable tick.
4. Render input snapshot.
5. Renderer begin frame.
6. Runtime render extraction: ECS queries, runtime sidecars, dirty-domain interpretation, deletion cleanup, and `IRenderer::SubmitRuntimeSnapshots()` handoff.
7. Renderer render-world extraction.
8. Render prepare.
9. Render execute.
10. End frame + present.
11. Maintenance: transfer retirement, streaming drain/apply/pump, asset service tick.
12. Frame clock finalize.

## Streaming integration

`Extrinsic.Runtime.StreamingExecutor` is the primary persistent async streaming
execution path. `Engine` still carries a temporary compatibility bridge for
legacy `GetStreamingGraph()` producers while migration is in progress.

Shutdown order requirement:

1. `StreamingExecutor::ShutdownAndDrain()`
2. task scheduler teardown

## Dependency direction

`Runtime` depends on `Core`, `Assets`, `ECS`, `Platform`, and `Graphics`.
It orchestrates but does not own GPU barrier/resource policy or render-pass
branching logic.

`Extrinsic.Runtime.RenderExtraction` is the only promoted runtime owner for live
ECS render queries. It stores entity-to-graphics sidecars outside canonical ECS
components, allocates/frees `GpuWorld` instance handles through the renderer,
builds transform/light/visualization/gizmo snapshot records, and submits those
records to graphics through `IRenderer::SubmitRuntimeSnapshots()`. It also owns
the `GRAPHICS-023C` asset-generation observation bridge: when a renderable has
`AssetInstance::Source`, extraction can compare the normalized `Assets::AssetId`
with a supplied `Graphics::GpuAssetCache` view and `GpuSceneSlot` metadata to
report pending/up-to-date/rebind-required states without performing the later
GPU geometry or material rebind.

Runtime owns camera motion, input-to-pick-request translation, gizmo hit testing,
and transform application. Graphics receives only immutable `CameraViewInput`,
`PickPixelRequest`, and transform-gizmo render packets during extraction.

