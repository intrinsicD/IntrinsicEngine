# Runtime Subsystem Boundaries & Lifecycle Map

This document is the current baseline map for the runtime-side subsystem split introduced around `Engine`, `GraphicsBackend`, `AssetPipeline`, `SceneManager`, and `RenderOrchestrator`.

It answers three questions:

1. **Who owns what?**
2. **Which module layers may depend on which others?**
3. **What is the concrete startup / per-frame / shutdown order today?**

The intent is architectural clarity, not future-state redesign. This file should describe the code that exists now.

---

## 1. Runtime Ownership Map

```text
Engine
├── Window
├── FeatureRegistry
├── SelectionModule
├── IO backend + IO registry
├── SceneManager
│   └── ECS::Scene / entt::registry
├── GraphicsBackend
│   ├── VulkanContext
│   ├── VulkanDevice
│   ├── Surface
│   ├── VulkanSwapchain
│   ├── SimpleRenderer
│   ├── TransferManager
│   ├── DescriptorLayout / DescriptorAllocator
│   ├── BindlessDescriptorSystem
│   ├── TextureSystem
│   └── optional CudaDevice
├── AssetPipeline
│   ├── AssetManager
│   ├── pending transfer completion state
│   ├── main-thread task queue
│   └── runtime-created material keep-alive list
└── RenderOrchestrator
    ├── frame allocators (LinearArena / ScopeStack)
    ├── FrameGraph
    ├── GeometryPool
    ├── ShaderRegistry
    ├── DebugDraw
    ├── MaterialSystem
    ├── PipelineLibrary
    ├── GPUScene
    └── RenderSystem
```

### Boundary summary

#### `Engine`

- **Role:** top-level composition root and frame-loop owner.
- **Owns directly:** window, feature registry, selection module, I/O backend, I/O registry, and the four extracted subsystems.
- **May orchestrate:** startup ordering, resize handling, fixed-step lane, variable-step lane, render handoff, asset upload polling, and final shutdown sequencing.
- **Should not absorb again:** low-level Vulkan lifetime management, direct asset-transfer bookkeeping, ECS hook wiring details, or render-subsystem internal ownership.

#### `GraphicsBackend`

- **Role:** owns the complete GPU platform stack and its destruction order.
- **Owns:** context, logical device, presentation surface, swapchain, renderer, transfer manager, descriptor plumbing, bindless system, texture system, default texture, and optional CUDA device.
- **Exports borrowed accessors:** `GetDevice()`, `GetSwapchain()`, `GetTransferManager()`, `GetBindlessSystem()`, `GetTextureSystem()`, and related descriptor accessors.
- **Does not own:** scene state, materials as engine concepts, render-graph policy, ECS systems, or asset database policy.

#### `AssetPipeline`

- **Role:** asset-state bridge between async GPU transfer completion and main-thread finalization.
- **Owns:** `Core::Assets::AssetManager`, pending transfer records, main-thread deferred task queue, and tracked material handles.
- **Borrows:** `RHI::TransferManager&` from `GraphicsBackend`.
- **Does not own:** I/O decoding policy, window/rendering state, ECS scene, or GPU resource factories.

#### `SceneManager`

- **Role:** ECS scene authority and entity-lifecycle coordination.
- **Owns:** `ECS::Scene`, registry access, spawn/clear operations, and GPU cleanup hook connection/disconnection.
- **Borrows/configures:** `Graphics::GeometryPool*` for topology inspection during spawn and `Graphics::GPUScene&` for immediate slot reclaim hooks.
- **Does not own:** render pipelines, asset transfer state, or device-level resources.

#### `RenderOrchestrator`

- **Role:** runtime render subsystem owner.
- **Owns:** frame allocators, `Core::FrameGraph`, shader registry, geometry pool, debug draw accumulator, material system, pipeline library, GPU scene, and render system.
- **Borrows:** device, swapchain, renderer, bindless, descriptor pool/layout, texture system, and asset manager.
- **Does not own:** the window, swapchain recreation policy outside its resize entrypoint, scene registry, or asset ingestion queues.

---

## 2. Current Inter-Subsystem Dependency Directions

The runtime split is intentionally directional.

```text
Core ───────► RHI ───────► Graphics
  │             │            │
  │             └────────────┘
  │
  ├──────► ECS
  ├──────► Geometry
  └──────► Runtime

Geometry ─► Graphics        (geometry data / algorithms consumed by rendering)
ECS ─────► Graphics         (render components + systems operate on ECS state)

Runtime ─► Core
Runtime ─► RHI
Runtime ─► Graphics
Runtime ─► ECS
Runtime ─► Geometry
Runtime ─► Interface
```

### Practical reading of the dependency graph

- **`Core`** is the bottom utility layer: memory, assets, frame graph, hashing, telemetry, tasks, feature registry, I/O abstractions, and windowing.
- **`RHI`** depends on `Core`-level facilities and encapsulates Vulkan/CUDA-facing primitives.
- **`Graphics`** depends on `RHI`, `Core`, `Geometry`, and `ECS` to implement render passes, render graph execution, geometry upload/sync systems, materials, and GPU scene infrastructure.
- **`Geometry`** stays algorithm/data-structure focused and does not depend on `Runtime`.
- **`ECS`** stays scene/component/system infrastructure focused and does not depend on `Runtime`.
- **`Interface`** is a top-layer UI/editor integration surface that can consume runtime and graphics state but should not become a dependency of lower layers.
- **`Runtime`** is the composition layer that wires the others together into an application loop.

### Allowed dependency intent by module family

| Module family | May depend on | Should not depend on |
|---|---|---|
| `Core` | STL / third-party utility libs | `Runtime`, `Graphics`, `RHI`, `Interface` |
| `Geometry` | `Core` | `Runtime`, `Graphics`, `Interface` |
| `ECS` | `Core`, narrow math/types as needed | `Runtime`, `Interface` |
| `RHI` | `Core` | `Runtime`, `Graphics`, `Interface` |
| `Graphics` | `Core`, `RHI`, `Geometry`, `ECS` | `Runtime` |
| `Interface` | `Core`, `Graphics`, `Runtime`, `ECS` as presentation glue | lower layers depending back on `Interface` |
| `Runtime` | all lower/runtime-adjacent layers above | being depended on by lower layers |

### Current subsystem borrowing edges

Within the runtime composition root, today’s direct borrowing edges are:

- `Engine -> GraphicsBackend`: owns subsystem, borrows accessors for device/swapchain/transfer/descriptor/texture services.
- `Engine -> AssetPipeline`: owns subsystem, delegates asset-manager access, transfer completion polling, and main-thread queue execution.
- `Engine -> SceneManager`: owns subsystem, delegates scene access, spawn, clear, and GPU-hook connect/disconnect.
- `Engine -> RenderOrchestrator`: owns subsystem, delegates frame allocators, frame graph, geometry storage, material system, GPU scene, and render system access.
- `AssetPipeline -> GraphicsBackend`: **borrowed edge only** through `RHI::TransferManager&`.
- `RenderOrchestrator -> GraphicsBackend`: **borrowed edges only** through device/swapchain/renderer/bindless/descriptors/texture system.
- `RenderOrchestrator -> AssetPipeline`: **borrowed edge only** through `Core::Assets::AssetManager&`.
- `SceneManager -> RenderOrchestrator`: optional GPU-scene hook connection plus geometry-pool pointer for spawn-time topology inspection.

This is the current “diamond” structure: `Engine` owns all four subsystems, while carefully selected borrowed references cross between them.

---

## 3. One-Page Runtime Lifecycle Map

## Startup order

```text
1. Engine config / benchmark setup
2. Task scheduler + file watcher init
3. SceneManager
4. Window
5. GraphicsBackend
6. AssetPipeline
7. ImGui / Interface::GUI init
8. RenderOrchestrator
9. SceneManager GPU hook connection + geometry storage wiring
10. FeatureRegistry population
11. IO backend + builtin loader registration
```

### Why this order is valid

- `SceneManager` is independent and can exist before GPU state.
- `GraphicsBackend` requires a valid window for surface creation.
- `AssetPipeline` requires `GraphicsBackend`’s transfer manager.
- `RenderOrchestrator` requires GPU infrastructure plus the asset manager.
- `SceneManager` GPU hooks are connected only after `RenderOrchestrator` has a `GPUScene`.

## Per-frame order

```text
Begin telemetry
  ├─ reset frame allocators / DebugDraw
  ├─ poll window events
  ├─ detect + apply resize
  ├─ process main-thread asset queue
  ├─ process completed uploads
  ├─ process texture/material deferred deletions
  ├─ fixed-step lane (0..N ticks)
  │    ├─ client OnFixedUpdate
  │    ├─ fixed-step FrameGraph registration
  │    └─ compile + execute fixed graph inside AssetManager read phase
  ├─ variable update lane
  │    ├─ client OnUpdate
  │    ├─ register core ECS systems in FrameGraph
  │    └─ compile + execute frame graph inside AssetManager read phase
  ├─ pump async point-cloud k-means completions
  ├─ drain dispatcher events
  ├─ transfer-manager garbage collection
  ├─ client OnRender
  ├─ write telemetry / benchmark frame
End telemetry
```

## Current variable-step system order

The variable-dt `FrameGraph` is assembled in this order, subject to feature toggles and GPUScene availability:

1. Client/gameplay systems via `OnRegisterSystems(...)`
2. `TransformUpdate`
3. `PropertySetDirtySync`
4. `PrimitiveBVHSync`
5. `GraphGeometrySync`
6. `MeshRendererLifecycle`
7. `PointCloudGeometrySync`
8. `MeshViewLifecycle`
9. `GPUSceneSync`

The actual execution order is still data-dependency-driven by `Core::FrameGraph`, but this is the current registration order in `Engine::Run()`.

## Shutdown order

```text
1. GraphicsBackend::WaitIdle()
2. SceneManager::DisconnectGpuHooks()
3. Task scheduler shutdown
4. File watcher shutdown
5. MaterialSystem deferred deletion processing
6. SceneManager::Clear()
7. AssetManager::Clear()
8. AssetPipeline tracked-material clear
9. RenderOrchestrator destruction
10. Interface::GUI shutdown
11. AssetPipeline destruction
12. SceneManager destruction
13. GraphicsBackend destruction
14. Window destruction
15. Final task wait + graphics deletion flush after Run() exits
```

### Shutdown invariants

- GPU-dependent scene hooks are disconnected before GPU owners are destroyed.
- Material deletions are processed before `RenderOrchestrator` tears down `MaterialSystem`.
- `RenderOrchestrator` dies before the GUI backend that may still reference render-owned textures.
- `GraphicsBackend` dies after all higher-level runtime subsystems that borrow its services.

---

## 4. Known Coupling Hotspots Captured by This Baseline

This baseline also makes the current hot spots explicit:

- `Engine::Run()` still owns most orchestration policy directly.
- `SceneManager` still uses file-static GPU hook state for EnTT destruction callbacks.
- Runtime-to-runtime borrowing is still manual rather than lane-coordinated (`AssetPipeline`, `SceneManager`, `RenderOrchestrator` all meet in `Engine`).
- Core ECS system registration is still handwritten in the main frame loop.

Those are intentional observations for the follow-up TODO items; they are not contradictions in this document.
