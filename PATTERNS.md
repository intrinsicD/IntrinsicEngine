# IntrinsicEngine — Reusable Patterns Catalog

This document catalogs the main reusable patterns in the IntrinsicEngine codebase, with canonical examples and guidance on when/how to apply each pattern.

---

## 1. Subsystem Injection Pattern

**What:** Large subsystems are non-copyable, non-movable classes with deleted copy/move constructors. Dependencies are accepted as borrowed references in the constructor and stored as non-owning references (`&` or `*`). Owning references use `std::shared_ptr` or `std::unique_ptr`.

**Canonical examples:**
- `Runtime.GraphicsBackend.cppm` — Takes `Core::Windowing::Window&` and config, owns Vulkan context/device/swapchain via `unique_ptr`, provides `GetDevice()`, `GetSwapchain()` accessors returning non-owning references.
- `Runtime.AssetPipeline.cppm` — Takes borrowed `RHI::TransferManager&`, owns `Core::Assets::AssetManager` and thread-safe queues.
- `Runtime.RenderOrchestrator.cppm` — Takes 8 borrowed references (device, swapchain, renderer, bindless, etc.), owns `PipelineLibrary`, `MaterialSystem`, `GPUScene`, `RenderSystem`.
- `Runtime.SceneManager.cppm` — Owns `ECS::Scene`, takes `Graphics::GPUScene&` in `ConnectGpuHooks()`.

**Instantiation order** (in `Engine.cpp`):
1. `GraphicsBackend` → owns GPU infrastructure
2. `AssetPipeline` → takes ref to `TransferManager` from GraphicsBackend
3. `RenderOrchestrator` → takes refs to GPU resources from GraphicsBackend
4. `SceneManager` → takes ref to `GPUScene` from RenderOrchestrator

**Use when:**
- Adding a new engine-level subsystem (audio, physics, networking).
- The subsystem owns significant infrastructure and needs a deterministic construction/destruction order.
- You need explicit dependency declarations — no hidden coupling, no global state, no service locator.

---

## 2. Error Handling with `std::expected`

**What:** Two distinct styles based on pipeline complexity:

### Monadic chaining (`.and_then()`, `.transform()`, `.or_else()`)
Use when: 3+ fallible stages, each consuming the prior result, error forwarding unchanged.

```cpp
// Graphics.Importers.STL.cpp — 2-stage pipeline
return decodeGeometry(data)
    .transform([](GeometryCpuData&& geometry) -> ImportResult {
        MeshImportData importData;
        importData.Meshes.push_back(std::move(geometry));
        return ImportResult{std::move(importData)};
    });
```

### Direct if-check style
Use when: 1-2 fallible calls, branching logic dominates, or format-specific conditional paths.

```cpp
// Graphics.Importers.OBJ.cpp — conditional parsing with branches
const auto x = TextParse::ParseNumber<float>(tokens[1]);
const auto y = TextParse::ParseNumber<float>(tokens[2]);
const auto z = TextParse::ParseNumber<float>(tokens[3]);
if (x && y && z) tempPos.emplace_back(*x, *y, *z);
```

**Error type rule:** Keep a stable domain error enum (`AssetError`, `SceneError`, `ErrorCode`) and convert once at module boundaries.

**Canonical examples:**
- `Graphics.Importers.STL.cpp` — monadic chaining for decode → transform pipeline.
- `Graphics.Importers.OBJ.cpp` — direct if-check for multi-branch parsing.
- `Graphics.IORegistry.cppm` — `IAssetLoader::Load()` returns `std::expected<ImportResult, AssetError>`.
- `Runtime.SceneSerializer.cppm` — `SceneError` enum for scene persistence.

**Use when:**
- Import/export pipelines (read → decode → build GPU payload → register asset).
- Runtime initialization sequences (create device → create swapchain → create renderer).
- Any fallible operation — never use exceptions, never use silent failures.

---

## 3. Geometry Operator Pattern

**What:** Every geometry algorithm follows a strict Params-Result contract:

- **Params struct** with sensible defaults.
- **Result struct** with diagnostics (iteration counts, element counts, convergence status).
- **Return type:** `std::optional<Result>` — `std::nullopt` for degenerate input (empty mesh, zero iterations, collinear points).
- **In-place vs. new mesh:** Topology-modifying operations (simplification, remeshing) work in-place. Topology-restructuring operations (subdivision) take `const Mesh& input, Mesh& output`.
- **Safety limits:** All `CWRotatedHalfedge` loops have `if (++safety > N) break;` guards.

**Canonical examples:**
- `Geometry.Simplification.cppm` — `SimplificationParams { TargetFaces, MaxError, PreserveBoundary }`, returns `std::optional<SimplificationResult>`.
- `Geometry.CatmullClark.cppm` — `SubdivisionParams`, produces new mesh via `const Mesh& input, Mesh& output`.
- `Geometry.NormalEstimation.cppm` — `EstimationParams` with octree config, borrows point positions as `std::span<const glm::vec3>`.
- `Geometry.MarchingCubes.cppm` — `ScalarGrid` struct with linearized 3D indexing, `Extract()` returns indexed triangle soup.
- `Geometry.ConvexHullBuilder.cppm` — `ConvexHullParams`, returns `ConvexHull` struct (V-Rep + H-Rep).

**Use when:**
- Implementing any new mesh/graph/cloud processing algorithm.
- Copy `Simplification.cppm` as a template for the Params/Result/optional structure.
- Expose measurable quality diagnostics in Result so heuristics can be regression-tested.

---

## 4. Module Organization Pattern

**What:** C++23 modules with strict interface/implementation separation:

- **Interface (`.cppm`):** Export declarations, types, function signatures.
- **Implementation (`.cpp`):** Module implementation unit with function bodies.
- **Naming:** `Namespace.ComponentName.cppm` / `Namespace.ComponentName.cpp`.
- **Partitions:** Sub-modules using `:PartitionName` syntax (e.g., `module Graphics:Importers.OBJ;`).

**CMakeLists registration:**
```cmake
target_sources(${target_name}
    PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Core.Assets.cppm          # Interface
    PRIVATE
        Core.Assets.cpp           # Implementation
)
```

**Canonical examples:**
- `Core.Assets.cppm` + `Core.Assets.cpp` — interface/implementation pair.
- `RHI.Pipeline.cppm` + `RHI.Pipeline.cpp` — RHI partition with Device/Shader imports.
- `Graphics:Importers.OBJ` — nested module partition importing `Graphics:IORegistry`.

**Use when:**
- Adding any new subsystem or component to the engine.
- Always update `CMakeLists.txt` correctly — `.cppm` under `FILE_SET CXX_MODULES`, `.cpp` under `PRIVATE`.

---

## 5. ECS Component Patterns

**What:** Three component categories with distinct roles:

### Zero-Size Tag Components
Presence/absence is the signal — no boolean flags.

```cpp
namespace ECS::DirtyTag {
    struct VertexPositions {};
    struct VertexAttributes {};
    struct EdgeTopology {};
    // ... 6 total dirty tags
}
struct SelectedTag {};
struct HoveredTag {};
struct SelectableTag {};
```

### Data Components with GPU State
Hold CPU/GPU state, geometry references, cached computed data.

```cpp
namespace ECS::PointCloud {
    struct Data {
        std::shared_ptr<Geometry::PointCloud::Cloud> CloudRef;
        Geometry::GeometryHandle GpuGeometry{};
        uint32_t GpuSlot = kInvalidSlot;
        std::vector<uint32_t> CachedColors;
        std::vector<float> CachedRadii;
        bool GpuDirty = true;
    };
}
```

### Lifecycle Hooks (`on_destroy`)
Synchronous, immediate resource cleanup when a component is removed.

```cpp
// Runtime.SceneManager.cpp
registry.on_destroy<ECS::Surface::Component>().connect<&OnSurfaceDestroyed>();
// Hook frees GPUScene slot immediately — no deferred event
```

**Canonical examples:**
- `Graphics.Components.cppm` — `Surface::Component`, `Line::Component`, `Point::Component`, `Graph::Data`, `PointCloud::Data`, `DirtyTag::*`.
- `ECS.Components.Transform.cppm` — `IsDirtyTag`, `WorldUpdatedTag`.
- `ECS.Components.Selection.cppm` — `SelectableTag`, `SelectedTag`, `HoveredTag`.
- `Runtime.SceneManager.cpp` — `on_destroy` hooks for GPU resource cleanup.

**Use when:**
- **Tags:** Per-entity dirty tracking, selection state, visibility toggling. The toggle is attach/detach — not a boolean field.
- **Data components:** When an entity needs owned resources and GPU state. Include `GpuDirty` flag and cached attribute vectors.
- **on_destroy hooks:** When GPU resources (GPUScene slots, descriptor sets, buffers) must be freed synchronously and immediately on component removal.

---

## 6. Render Pass / IRenderFeature Pattern

**What:** All render passes inherit from `IRenderFeature` and register with `DefaultPipeline`:

```cpp
class IRenderFeature {
    virtual ~IRenderFeature() = default;
    virtual void Initialize(VulkanDevice& device,
                           DescriptorAllocator& descriptorPool,
                           DescriptorLayout& globalLayout) = 0;
    virtual void AddPasses(RenderPassContext& ctx) = 0;
    virtual void Shutdown() {}
    virtual void OnResize(uint32_t width, uint32_t height) {}
};
```

**Key design:**
- Each pass owns its pipelines and descriptor set layouts 1+ (set 0 is global camera, reserved).
- BDA push constants eliminate vertex-input-binding calls — zero `vkCmdBindVertexBuffers` in the codebase.
- Retained geometry uses persistent device-local buffers; transient debug content uses per-frame host-visible buffers.
- CPU-side frustum culling via `FrustumCullSphere()` in `PassUtils.hpp`.

**Canonical examples:**
- `Graphics.Passes.Surface.cppm` — Filled triangles with BDA from `GeometryGpuData`.
- `Graphics.Passes.Line.cppm` — Thick anti-aliased edges, retained + transient `DebugDraw::GetLines()`.
- `Graphics.Passes.Point.cppm` — Billboard quads with per-mode pipelines (`FlatDisc`/`Surfel`).
- `Graphics.Pipelines.cppm` — `DefaultPipeline` registers 10 stages in order.

**Use when:**
- Adding a new primitive type or rendering method.
- Steps: (1) Create `IRenderFeature` subclass, (2) Register in `DefaultPipeline::Initialize()`, (3) Declare FrameGraph dependencies, (4) Use BDA push constants for vertex data.

---

## 7. Frame Graph / DAG Scheduling Pattern

**What:** Per-frame system execution order via declared data dependencies:

- **`Read<T>()`** — reads component, may parallelize with other readers.
- **`Write<T>()`** — writes component, serializes with all reads/writes of that type.
- **`WaitFor(label)`** — waits for a named signal from earlier passes.
- **`Signal(label)`** — declares this pass fulfills a named stage.

All per-frame transient data (pass nodes, adjacency lists, closures) allocated in a `ScopeStack` — zero per-frame heap allocation.

**Canonical examples:**
- `Core.FrameGraph.cppm` — Compile-time type IDs via `Core::TypeToken<T>()`.
- `ECS.Systems.Transform.cppm` — Declares `Write<Transform::Component>`, signals `"TransformUpdate"`.
- `Graphics.Systems.PropertySetDirtySync.cppm` — `WaitFor("TransformUpdate")`, signals `"PropertySetDirtySync"`.
- `Graphics.Systems.MeshViewLifecycle.cppm` — `WaitFor("PropertySetDirtySync")`.

**Execution order:** PropertySetDirtySync → MeshViewLifecycle → GraphGeometrySync → PointCloudGeometrySync → GPUSceneSync.

**Use when:**
- Adding any new ECS system that runs per-frame.
- Steps: (1) Implement `OnUpdate(registry, ...)`, (2) Implement `RegisterSystem(graph, registry, ...)` calling `graph.AddPass()`, (3) Declare Read/Write/WaitFor/Signal dependencies precisely.

---

## 8. GPU Resource Management / SafeDestroy Pattern

**What:** Timeline-based deferred destruction with `InplaceFunction` (small-buffer-optimized, no heap):

```cpp
// Move containers into locals first, then move-capture
std::vector<VkDescriptorPool> pools = std::move(m_AllPools);
m_Device.SafeDestroy([dev, pools = std::move(pools)]() {
    for (auto pool : pools) vkDestroyDescriptorPool(dev, pool, nullptr);
});
```

**Critical constraint:** `InplaceFunction` requires `is_nothrow_move_constructible`. Never capture a `const` container by value — it prevents move construction, failing the static assert.

**Canonical examples:**
- `RHI.Device.cppm` — `SafeDestroy()`, `SafeDestroyAfter()`, `CollectGarbage()`.
- `Core.InplaceFunction.cppm` — Small-buffer-optimized callable.

**Use when:**
- Any subsystem holding Vulkan resources with non-trivial destruction.
- Steps: (1) Capture device pointer, (2) Move-capture containers, (3) Call `SafeDestroy()`, (4) Never use exceptions in the lambda.

---

## 9. Commit Hygiene for Cross-Cutting Render Contracts

**What:** Separate **feature-local work** from **cross-cutting render-contract changes** whenever a change affects canonical frame construction, resource requirements, or default pipeline behavior.

**Why:** Helpers such as `BuildDefaultPipelineRecipe(...)` sit on the critical path for every pipeline configuration. A change there is not just a local implementation detail — it changes the global frame recipe contract:

- which canonical resources become required,
- whether deferred / hybrid / forward fallback occurs,
- whether picking sideband resources are allocated,
- and whether debug or selection overlays become legal for a given frame.

Bundling that kind of rewrite into an unrelated feature commit makes review noisy and obscures whether regressions come from the feature itself or from the renderer-wide contract change.

**Use when:**
- A feature also needs a behavioral rewrite of `BuildDefaultPipelineRecipe(...)`, render-graph validation policy, resource definitions, or pass-order rules.
- A refactor changes default behavior for *all* pipelines rather than only the new feature’s code path.
- The diff mixes “new pass / new feature” work with “existing renderer contract changed everywhere” work.

**Preferred split:**
1. **Commit A — contract/mechanical change**
   - Adjust recipe/resource behavior.
   - Add or update focused regression tests.
   - Keep feature-facing code out of this commit when practical.
2. **Commit B — feature integration**
   - Add the new pass, shaders, UI, or geometry algorithm.
   - Consume the already-reviewed contract behavior from Commit A.

**Canonical examples:**
- `src/Runtime/Graphics/Pipelines/Graphics.Pipelines.cpp` — `BuildDefaultPipelineRecipe(...)` is a renderer-wide contract seam, not a feature-local helper.
- `tests/Test_RuntimeGraphics.cpp` — focused recipe regression coverage should land with contract changes.
- `tests/Test_CompositionAndValidation.cpp` — render-graph validation expectations should be updated in the same contract-focused commit series.

---

## 10. Event Communication Pattern

**What:** Three mechanisms for inter-system communication, each with distinct use cases:

### `entt::dispatcher` — Cross-System Notifications
Value-type event structs in `ECS::Events`. All sinks run on main thread during `dispatcher.update()`.

**Use for:** Selection/hover changed, entity spawned/destroyed, geometry modified, async GPU readback completed.
**Don't use for:** Per-component dirty tracking (→ `DirtyTag`), GPU cleanup (→ `on_destroy`), high-frequency per-entity data (→ direct ECS queries), cross-thread (→ `RunOnMainThread()` queue first).

### `DirtyTag::*` — Per-Entity Incremental Sync
Zero-size tag components consumed by `PropertySetDirtySyncSystem`. Efficient when many entities may be dirty simultaneously.

### `on_destroy` Hooks — Lifecycle Cleanup
Synchronous and immediate. Fires during `reg.remove<T>()` or `reg.destroy()`. Never use deferred events for cleanup that must complete before next allocation.

**Canonical examples:**
- `ECS.Components.Events.cppm` — `SelectionChanged`, `HoverChanged`, `GpuPickCompleted`, `EntitySpawned`, `GeometryModified`.
- `Graphics.Components.cppm` — `DirtyTag::VertexPositions` through `DirtyTag::FaceAttributes`.
- `Runtime.SceneManager.cpp` — `on_destroy` hooks for GPUScene slot cleanup.

---

## 11. Asset Loader/Exporter Interface Pattern

**What:** Stateless transform interface — loaders/exporters never open files; they receive bytes from `IIOBackend` via `LoadContext`:

```cpp
class IAssetLoader {
    virtual std::string_view FormatName() const = 0;
    virtual std::span<const std::string_view> Extensions() const = 0;
    virtual std::expected<ImportResult, AssetError> Load(
        std::span<const std::byte> data, const LoadContext& ctx) = 0;
};
```

**Registration via IORegistry:** Stores loaders/exporters in vectors, indexes by extension (lowercased). `RegisterBuiltinLoaders()` adds OBJ, PLY, XYZ, PCD, TGF, GLTF, STL, OFF.

**Canonical examples:**
- `Graphics.IORegistry.cppm` — `IAssetLoader`, `IAssetExporter`, registration and dispatch.
- `Graphics.Importers.STL.cpp` — Monadic `Load()` implementation.
- `Graphics.Importers.OBJ.cppm` — Direct if-check `Load()` implementation.

**Use when:**
- Adding a new file format.
- Steps: (1) Inherit `IAssetLoader` or `IAssetExporter`, (2) Implement `FormatName()`, `Extensions()`, `Load()`/`Export()`, (3) Define destructor in `.cpp` for vtable anchoring, (4) Register via `RegisterLoader(std::make_unique<MyLoader>())`.

---

## 12. Lifecycle System Pattern

**What:** Two-phase GPU upload with PropertySet attribute extraction:

1. **Phase 1 — Detect dirty:** Check `GpuDirty` flag on geometry data component.
2. **Phase 2 — Upload & allocate:** Compact positions (skip deleted vertices, build remap table), upload via Direct (host-visible, dynamic) or Staged (device-local, static), extract per-element attributes from PropertySets into cached vectors, allocate GPUScene slot.
3. **Phase 3 — Populate per-pass components:** Idempotently set `Line::Component` and `Point::Component` every frame from completed GPU geometry.

**Upload mode selection:** `StaticGeometry = false` (default) → Direct (host-visible) for dynamic graphs; `true` → Staged (device-local) for static content.

**Naming convention:** Systems that create derived geometry views (edge/vertex views from existing mesh buffers) use the `*Lifecycle` suffix. Systems that upload geometry from an authoritative data source (graph, point cloud) use the `*Sync` suffix. Both implement the same three-phase pattern; the distinction is in their data flow direction. `PropertySetDirtySync` and `GPUSceneSync` are infrastructure systems (not per-geometry-type lifecycle) and correctly use the `Sync` suffix.

**Canonical examples:**
- `Graphics.Systems.MeshViewLifecycle.cppm` — Creates edge index buffers from collision data, populates `Line::Component` and `Point::Component` via `ReuseVertexBuffersFrom`.
- `Graphics.Systems.GraphGeometrySync.cppm` — Uploads graph positions/normals/edge pairs, supports Direct/Staged upload mode per-entity.
- `Graphics.Systems.PointCloudGeometrySync.cppm` — Reads `Cloud::Positions()`/`Normals()` spans (zero copy), supports preloaded-geometry entities.

**Use when:**
- Adding a new geometry type to the engine.
- Steps: (1) Create a `Data` component holding `shared_ptr<GeometryType>`, (2) Add `GpuDirty` flag and cached attribute vectors, (3) Create `RegisterSystem()` with `Write<Data>` and `WaitFor("PropertySetDirtySync")`, (4) Check `GpuDirty`, upload, extract attributes, allocate GPUScene slot, (5) Populate `Line`/`Point` components every frame, (6) Register `on_destroy` hooks for GPUScene cleanup.

---

## 13. Vtable Anchor Pattern

**What:** When a class with virtual functions is declared in a module partition interface (`.cppm`), vtable emission can be fragile. As defensive practice:

- **Pure-virtual base classes:** Define the destructor in a single known TU (e.g., `Graphics.IORegistry.cpp` defines all loader and exporter destructors).
- **Non-pure-virtual base classes:** Define `~Base() = default;` out-of-line in the `.cppm` partition.

**Canonical examples:**
- `Graphics.IORegistry.cpp` — Defines destructors for all 8 loaders and 3 exporters.
- `Graphics.Pipelines.cppm` — `DefaultPipeline::~DefaultPipeline() = default;` as vtable anchor.

**Use when:**
- Declaring any virtual interface in a module partition. Always anchor the vtable by defining the destructor in exactly one TU.

---

## 14. BDA Shared-Buffer Design

**What:** One device-local vertex buffer on the GPU, multiple index buffers with different topologies referencing into it via buffer device addresses (BDA). Zero `vkCmdBindVertexBuffers` calls in the codebase.

A mesh uploads positions/normals once; wireframe, vertex visualization, and kNN graph edges all `ReuseVertexBuffersFrom` that mesh handle — zero vertex duplication.

**Push constants per pass:**
- **SurfacePass:** `uint64_t PtrPositions`, `PtrNormals`, `PtrAux` + `PtrFaceAttr`.
- **LinePass:** `uint64_t PtrPositions` + `PtrEdges` + `PtrEdgeAttr`.
- **PointPass (120 bytes):** Model + `PtrPositions`/`PtrNormals`/`PtrAttr` + PointSize/SizeMultiplier/Viewport + Color/Flags.

**Use when:**
- Adding a new visualization that shares vertex data with an existing geometry type.
- Use `ReuseVertexBuffersFrom()` to reference the shared buffer and create only a new index buffer for the new topology view.

---

## Summary Table

| # | Pattern | Key Files | Primary Use Case |
|---|---------|-----------|------------------|
| 1 | **Subsystem Injection** | `Runtime.GraphicsBackend.cppm`, `Runtime.AssetPipeline.cppm`, `Runtime.RenderOrchestrator.cppm` | New engine-level subsystems |
| 2 | **`std::expected` Error Handling** | `Graphics.Importers.STL.cpp`, `Graphics.IORegistry.cppm` | All fallible operations |
| 3 | **Geometry Operator** | `Geometry.Simplification.cppm`, `Geometry.CatmullClark.cppm`, `Geometry.ConvexHullBuilder.cppm` | New mesh/graph/cloud algorithms |
| 4 | **Module Organization** | `Core.Assets.cppm`/`.cpp`, `RHI.Pipeline.cppm`/`.cpp` | Every new source file |
| 5 | **ECS Components** | `Graphics.Components.cppm`, `ECS.Components.Selection.cppm` | New entity data types |
| 6 | **Render Pass / IRenderFeature** | `Graphics.Passes.Surface.cppm`, `Graphics.Pipelines.cppm` | New primitive types or rendering methods |
| 7 | **Frame Graph / DAG Scheduling** | `Core.FrameGraph.cppm`, system `RegisterSystem()` functions | New per-frame ECS systems |
| 8 | **SafeDestroy / Deferred Destruction** | `RHI.Device.cppm`, `Core.InplaceFunction.cppm` | GPU resource cleanup |
| 9 | **Event Communication** | `ECS.Components.Events.cppm`, `Graphics.Components.cppm` (DirtyTag) | Inter-system notifications |
| 10 | **Asset Loader/Exporter** | `Graphics.IORegistry.cppm`, `Graphics.Importers.*.cpp` | New file format support |
| 11 | **Lifecycle System** | `Graphics.Systems.MeshViewLifecycle.cppm`, `Graphics.Systems.GraphGeometrySync.cppm` | New geometry type → GPU pipeline |
| 12 | **Vtable Anchor** | `Graphics.IORegistry.cpp`, `Graphics.Pipelines.cppm` | Virtual interfaces in modules |
| 13 | **BDA Shared-Buffer** | Surface/Line/Point passes, `ReuseVertexBuffersFrom()` | Shared vertex data with multiple topology views |

---

## Worth Adopting — Patterns Not Yet In the Codebase

### 14. Command Pattern for Undo/Redo (P1)

**Gap:** ROADMAP.md Phase 2 lists "Undo/redo stack" but no implementation exists. The engine has `Core::Tasks` for async work but no Command abstraction for reversible operations.

**Recommendation:** New module `Core.Command.cppm`:
- `Core::Command` base with `Execute()` and `Undo()`.
- `Core::CompositeCommand` for transaction grouping (e.g., multi-entity transform).
- `Core::CommandHistory` with fixed-size ring buffer.
- Commands are non-copyable, movable, stored as `std::unique_ptr<Command>`.
- Use `std::expected` for execution results (not exceptions).

**Use when:**
- **Transform gizmo edits:** Each drag produces a `TransformCommand` capturing before/after state for all selected entities.
- **Property panel mutations:** Material color, point size, line width — each discrete change is a command.
- **Entity lifecycle:** Create/delete entity wrapped as commands. `CompositeCommand` groups entity + all its components.
- **Geometry operator application:** Simplification, subdivision, smoothing — capture mesh state before operator, undo reverts to snapshot.
- **Scene hierarchy changes:** Reparenting entities via drag-and-drop in the hierarchy panel.

**Priority:** P1 — directly enables the ROADMAP Phase 2 undo/redo item.

---

### Rejected Patterns (with rationale)

#### ~~15. Enumerate/Zip Iteration Utilities~~ — DROPPED

C++23 provides `std::views::enumerate` and `std::views::zip` natively. The engine uses Clang 20+ which supports both. A custom `Core.Iterators.cppm` module is unnecessary — use the standard library directly. Opportunistic adoption of `std::views::enumerate` across manual index loops should be done when files are touched.

#### ~~16. ComponentGui Template Dispatch~~ — DROPPED

Codebase audit found only 6 sequential `reg.all_of<T>` checks in `InspectorController.cpp`. At this scale the explicit if-chain is clear and maintainable. The abstraction overhead (dispatch table, type-erased panel registration) is not justified until 10+ component types are inspectable. Re-evaluate if 5+ new component panels are added.

#### ~~17. Policy-Based Template Composition~~ — DROPPED

The `docs/architecture/algorithm-variant-dispatch.md` already provides a `std::variant` + `std::visit` pattern that covers runtime algorithm selection with compile-time exhaustiveness. Template policies would only add value for operators with 2+ orthogonal hot-loop variation axes — no existing operator qualifies. If a future operator (ICP, field design) genuinely needs multi-axis compile-time specialization, adopt the pattern locally at that time rather than pre-building infrastructure.

### Note: Lock-Free MPSC Queue — Already Covered

The existing `Utils::LockFreeQueue<T>` (`Utils.LockFreeQueue.cppm`) is **already MPMC** — it uses `compare_exchange_weak` on both `m_Tail` (producers) and `m_Head` (consumers) via Vyukov's bounded turn-based protocol. It is consumed by multiple worker threads via `Core.Tasks.cpp` (global inject queue, capacity 65536). A separate `Core.MPSCQueue.cppm` module is **not needed**. If contention increases under future Potree streaming or CUDA interop workloads, profile the existing queue first before introducing alternatives.

---

## Full Summary Table

| # | Pattern | Status | Priority | Primary Use Case |
|---|---------|--------|----------|------------------|
| 1 | Subsystem Injection | Implemented | — | New engine-level subsystems |
| 2 | `std::expected` Error Handling | Implemented | — | All fallible operations |
| 3 | Geometry Operator | Implemented | — | New algorithms |
| 4 | Module Organization | Implemented | — | Every new source file |
| 5 | ECS Components | Implemented | — | New entity data types |
| 6 | Render Pass / IRenderFeature | Implemented | — | New rendering methods |
| 7 | Frame Graph / DAG Scheduling | Implemented | — | New per-frame systems |
| 8 | SafeDestroy / Deferred Destruction | Implemented | — | GPU resource cleanup |
| 9 | Event Communication | Implemented | — | Inter-system notifications |
| 10 | Asset Loader/Exporter | Implemented | — | New file format support |
| 11 | Lifecycle System | Implemented | — | Geometry → GPU pipeline |
| 12 | Vtable Anchor | Implemented | — | Virtual interfaces in modules |
| 13 | BDA Shared-Buffer | Implemented | — | Shared vertex data topology views |
| 14 | Command Pattern (Undo/Redo) | **Not yet** | P1 | Reversible editor operations |
| ~~15~~ | ~~Enumerate/Zip Utilities~~ | Dropped | — | C++23 `std::views::enumerate`/`zip` covers this natively |
| ~~16~~ | ~~ComponentGui Dispatch~~ | Dropped | — | Only 6 component checks; not justified at current scale |
| ~~17~~ | ~~Policy-Based Composition~~ | Dropped | — | `std::variant` dispatch (Pattern doc) already covers the need |
