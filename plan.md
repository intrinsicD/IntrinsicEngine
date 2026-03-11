# Architectural Pattern Review — Engine23/Engine24 vs IntrinsicEngine

Review of patterns from two external C++23 engine codebases (Engine23, Engine24) against IntrinsicEngine's existing architecture. Categorized by applicability.

---

## Already Covered — No Action Needed

These patterns are already implemented in IntrinsicEngine, often in a superior form.

| External Pattern | IntrinsicEngine Equivalent | Notes |
|---|---|---|
| Service Locator (EnTT Locator) | Constructor-based DI (`GraphicsBackend`, `AssetPipeline`, `SceneManager`, `RenderOrchestrator`) | IntrinsicEngine's approach is architecturally stronger — no hidden coupling, no global state, explicit initialization order. |
| Strongly-Typed Handles (CRTP) | `Core::StrongHandle<Tag>` with generational safety | IntrinsicEngine's version adds generation counters for use-after-free protection. Strictly better. |
| Property System with Type Erasure + Dirty Tracking | `Geometry::PropertyRegistry` + `PropertyStorage<T>` + `DirtyTag::*` ECS components | Same SoA pattern with type erasure via `PropertyStorageBase`. Dirty tracking is per-entity via zero-size ECS tags rather than per-property booleans — more granular. |
| Entity Hierarchy (Intrusive Linked List) | `ECS::Components::Hierarchy` (Parent, FirstChild, NextSibling, PrevSibling, ChildCount) | Identical pattern. Already in place. |
| Geometry Data Model Hierarchy | `Halfedge::Mesh`, `Graph`, `PointCloud::Cloud` | Same layered design. IntrinsicEngine adds the operator pattern (Params/Result structs, `std::optional` return). |
| Mesh Circulators | Halfedge traversal via `CCWRotatedHalfedge()` / `CWRotatedHalfedge()` with safety limits | IntrinsicEngine uses explicit loop patterns with `++safety > N` guards instead of circulator objects. Functionally equivalent, more explicit about corruption safety. |
| FileWatcher with Callbacks | `Core::Filesystem::FileWatcher` | Background-thread polling with per-file callbacks. Already integrated with asset hot-reload. |
| Resource Pool with Free-List | `Core::ResourcePool<T, Handle, RetirementFrames>` | IntrinsicEngine's version adds deferred deletion (frame-aware retirement), shared_mutex for concurrent reads, and `std::expected` error returns. Superior. |
| Logging with Format Strings | `Core::Log::{Info,Warn,Error,Debug}` with `std::format_string<Args...>` | Compile-time format validation. Already in place. |
| System Lifecycle (pre_init / init / remove) | Deterministic Engine constructor/destructor ordering with RAII | IntrinsicEngine avoids the two-phase init complexity by using constructor DI — dependencies are resolved at construction time. Simpler. |
| BoundedHeap for KNN | `Utils::BoundedHeap<T>` in `Utils.BoundedHeap.cppm` | Already implemented with identical design (max-heap, threshold pruning, sorted output). Used by Octree KNN. |
| Render Batch Pattern | Bindless rendering with BDA push constants | IntrinsicEngine's approach is more modern — no per-batch shader binding, no global/local uniform split. BDA eliminates the need for batching entirely. |

---

## Not Applicable — Architectural Mismatch

These patterns conflict with IntrinsicEngine's design philosophy or are unnecessary given its technology choices.

| External Pattern | Why It Doesn't Fit |
|---|---|
| Event-Driven Game Loop (typed event phases) | IntrinsicEngine uses `FrameGraph` (DAG scheduler with Kahn's topological sort) for system ordering. This provides explicit dependency declaration, parallel execution of independent systems, and compile-time cycle detection. Strictly better than a linear event dispatch loop. |
| GLM-Eigen Zero-Copy Interop | IntrinsicEngine uses GLM exclusively. No Eigen dependency, no bridging needed. If Eigen is ever added (e.g., for spectral methods), this pattern would become relevant — but the DEC module currently uses custom CG solvers. |
| Configurable Scalar/Index Types | Vulkan has fixed-precision requirements for GPU buffers. The engine uses `float`/`uint32_t` consistently for GPU compatibility. Scientific double-precision is not a goal. |
| Domain-Specific Exception Hierarchy | IntrinsicEngine uses `std::expected` for error handling — no exceptions. This is an explicit, codebase-wide design decision documented in CLAUDE.md. |
| Plugin Architecture (friend lifecycle) | IntrinsicEngine uses `FeatureRegistry` for render feature registration and `FrameGraph` for system registration. Plugins as a concept don't exist — extensibility is through modules and feature flags. |
| Compile-Time Traits System (VecTraits) | Only one math library (GLM). The traits layer adds indirection without benefit when there's no polymorphism over math types. |
| Double-Buffered Command Queue | The `FrameGraph` (DAG scheduler) handles all system-level execution ordering. The only cross-thread queue is `RunOnMainThread()` for infrequent async completions (asset loads, file watcher callbacks) — contention is negligible. If it ever mattered, a lock-free MPSC queue would be the modern choice over double-buffering. |

---

## Worth Adopting — Actionable Items

These patterns address gaps in IntrinsicEngine or would improve existing code.

### 1. Command Pattern for Undo/Redo

**Source:** Engine23 Command Pattern (AbstractCommand / TaskCommand / CompositeCommand)

**Gap:** ROADMAP.md Phase 2 lists "Undo/redo stack: Command-pattern undo for all property changes" but no implementation exists. IntrinsicEngine has `Core::Tasks` (coroutine jobs) for async work but no Command abstraction for reversible operations.

**Recommendation:** Implement a minimal command interface in `Core`:

- `Core::Command` base with `Execute()` and `Undo()`.
- `Core::CompositeCommand` for transaction grouping (e.g., multi-entity transform).
- `Core::CommandHistory` with fixed-size ring buffer for undo/redo stack.
- Use `std::expected` for command execution results (not exceptions).
- Commands should be non-copyable, movable, stored as `std::unique_ptr<Command>` in the history.

**Scope:** New module `Core.Command.cppm`. Wire into `SceneManager` for property mutations and `TransformGizmo` for transform edits.

**Priority:** P1 — directly enables the ROADMAP Phase 2 undo/redo item.

### 2. Python-Like Iteration Utilities (Enumerate, Zip)

**Source:** Engine24 `Range()`, `Enumerate()`, `Zip()`, `SortByFirst()`

**Gap:** No equivalent exists in IntrinsicEngine. The codebase uses manual index tracking (`for (size_t i = 0; ...)`) throughout geometry processing, ECS iteration, and property sync code.

**Recommendation:** Add a small utility module `Core.Iterators.cppm` with:

- `Enumerate(container)` — returns `(index, value&)` pairs. Zero-cost via iterator adapter.
- `Zip(a, b)` — returns `(a_elem&, b_elem&)` pairs. Useful in property sync code.
- Skip `Range()` — C++23 `std::views::iota` covers this.
- Skip `SortByFirst()` — too specialized for a core utility.

**Priority:** P3 — quality-of-life improvement, not blocking any feature. Adopt opportunistically when touching files that would benefit.

### 3. ComponentGui — Template-Specialized Per-Type UI Rendering

**Source:** Engine23 `ComponentGui<T>::Show()`

**Gap:** IntrinsicEngine's `EditorUI` module renders property panels, but the pattern for per-component-type UI dispatch is not clear. If the current approach uses a centralized switch/if-chain over component types, it will become a maintenance bottleneck as more component types are added.

**Recommendation:** If not already using a dispatch pattern, introduce:

```cpp
template <typename T>
struct ComponentEditor {
    static void Render(entt::entity entity, entt::registry& reg);
};
```

Specialize per component type. The entity inspector iterates components and calls the appropriate specialization. This keeps domain logic (ECS components) decoupled from UI code (ImGui rendering).

**Priority:** P3 — architectural hygiene. Evaluate current `EditorUI` structure first; may already be clean enough.

### 4. Policy-Based Template Composition for Geometry Operators

**Source:** Engine23 OptimizerBase with orthogonal policy axes

**Gap:** IntrinsicEngine's geometry operators use enum-based policy selection (e.g., `LaplacianVariant::Combinatorial` vs `NormalizedSymmetric`). This works but requires runtime branching. For operators with multiple orthogonal variation points (solver strategy x weighting scheme x boundary handling), compile-time policy composition would be more efficient and extensible.

**Recommendation:** Consider for new operators in the "Top 8 next" list (ROADMAP Phase 5):

- **Heat method:** Solver policy (CG vs. Cholesky) x boundary policy (Dirichlet vs. Neumann).
- **Registration (ICP):** Correspondence policy (point-to-point vs. point-to-plane) x rejection policy (trimmed vs. robust).
- **Remeshing:** Field policy (cross-field vs. N-RoSy) x sizing policy (uniform vs. curvature-adaptive).

Use template policies only when: (a) there are 2+ orthogonal axes, (b) each axis has 2+ variants, and (c) the inner loop is hot enough that virtual dispatch matters.

**Priority:** P3 — adopt when implementing new geometry operators, not as a retrofit.

---

## Summary

| Action | Pattern | Priority | Blocks |
|---|---|---|---|
| **Implement** | Command Pattern (Undo/Redo) | P1 | ROADMAP Phase 2 |
| **Add** | Enumerate/Zip iteration utilities | P3 | Nothing (QoL) |
| **Evaluate** | ComponentGui template dispatch | P3 | Nothing (maintainability) |
| **Adopt selectively** | Policy-based operator composition | P3 | New geometry operators |

All other patterns from the Engine23/Engine24 catalogs are either already present in IntrinsicEngine (often in improved form) or conflict with its architectural principles.
