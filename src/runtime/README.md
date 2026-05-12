# Runtime

`src/runtime` is the composition root for the engine. It owns
subsystem instantiation order, frame-phase orchestration, and deterministic
startup/shutdown.

## Public module surface

| Module | Responsibility |
|---|---|
| `Extrinsic.Runtime.Engine` | Composition root, frame loop, subsystem wiring, app-facing reference engine config helper |
| `Extrinsic.Runtime.FrameLoop` | Testable platform/render/maintenance/shutdown phase contracts |
| `Extrinsic.Runtime.ProceduralGeometry` | Procedural-geometry descriptor surface (`ProceduralGeometryKey`, key hash, `ProceduralGeometryCache` value type with `EnsureResident` / `Release` / `Find`). Reuses the `ProceduralGeometryKind` enum and POD `ProceduralGeometryParams` defined in `Extrinsic.ECS.Component.ProceduralGeometryRef`. `EnsureResident(key, uploadDesc, uploadFn)` either invokes the injected upload functor exactly once on a new key or hits an existing entry and increments a `std::uint32_t` refcount; `Release(key)` decrements. N entities sharing `(Kind, Hash(Params))` share one `GpuGeometryHandle`. No live ECS, no graphics imports beyond the existing `Extrinsic.Graphics.GpuWorld` value-type edge. Extraction-tick wiring and deferred-retire policy are GRAPHICS-030-Impl-B and GRAPHICS-030-Impl-C respectively. |
| `Extrinsic.Runtime.ProceduralGeometryPacker` | Per-kind packer `Pack(kind, params, scratch) -> std::optional<GeometryUploadDesc>` consuming a runtime-owned `ProceduralGeometryPackBuffer` reused across ticks. Triangle is the only in-scope packer for Impl-A; the vertex layout is `{pos.xyz, uv}` (20 bytes/vertex) matching `Test.MinimalTriangleAcceptance`. Cube / Quad / Sphere / LineStrip extend the enum + packer table without cache or extraction lifecycle changes. |
| `Extrinsic.Runtime.RenderExtraction` | Runtime-owned ECS-to-graphics extraction cache and snapshot handoff |
| `Extrinsic.Runtime.StreamingExecutor` | Persistent background streaming task execution |

Planned modules (decisions recorded, implementation children identified but not yet opened):

| Planned module | Planning task | Responsibility |
|---|---|---|
| `Extrinsic.Runtime.ReferenceScene` | [`GRAPHICS-029` (done)](../../tasks/done/GRAPHICS-029-runtime-reference-scene-bootstrap.md) | Opt-in runtime-owned reference scene bootstrap. Exposes `IReferenceSceneProvider` and `ReferenceSceneRegistry`; `Engine::Initialize()` invokes the configured `EngineConfig::ReferenceScene::Selector` provider once, after scene registry construction, before the first frame. Default provider `TriangleProvider` creates one entity through `ECS::Scene::CreateDefault("ReferenceTriangle")` carrying HARDEN-060 default components plus exactly one `Graphics::Components::RenderSurface` and `ECS::Components::ProceduralGeometryRef{ Triangle }` (now that `Extrinsic.ECS.Component.ProceduralGeometryRef` exists). No GPU-typed value type attaches to the entity; camera authorship returns a `CameraViewInput` seed substituted into `RenderFrameInput::Camera` and forward-compatible with the GRAPHICS-017Q `CameraControllers` umbrella. |

`Extrinsic.Runtime.Engine` exports `CreateReferenceEngineConfig()` so reference
applications can request the standard runtime configuration without importing
lower-layer `core` config modules directly. Applications may pass the returned
config to `Engine`; runtime remains responsible for interpreting subsystem
configuration and composition. Per `GRAPHICS-029`, `CreateReferenceEngineConfig()`
will flip `EngineConfig::ReferenceScene::Enabled = true` and
`Selector = ReferenceSceneSelector::Triangle` once
`Extrinsic.Runtime.ReferenceScene` lands; the default-constructed
`EngineConfig{}` keeps `Enabled = false` so existing CPU/null tests do not
regress.

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

`Engine::RunFrame()` consumes `Extrinsic.Core.FrameClock` for wall-clock frame
delta sampling and post-sleep resampling; runtime owns the phase orchestration,
not the reusable clock value type.

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

