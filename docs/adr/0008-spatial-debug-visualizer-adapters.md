# ADR 0008 — Spatial Debug Visualizer Runtime Adapters

- **Status:** Accepted
- **Date:** 2026-05-17
- **Owners:** Runtime extraction (adapter ownership), Graphics (frozen `Extrinsic.Graphics.SpatialDebugVisualizers` packet contract)
- **Related tasks:** [`tasks/done/GRAPHICS-011`](../../tasks/archive/GRAPHICS-011-spatial-debug-visualizers.md), [`GRAPHICS-011Q`](../../tasks/archive/GRAPHICS-011Q-spatial-debug-adapter-clarifications.md)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`src/graphics/renderer/README.md`](../../src/graphics/renderer/README.md)
- **Supersedes:** none. Extracted from the `Extrinsic.Graphics.SpatialDebugVisualizers` bullet in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/archive/DOCS-001-reduce-graphics-architecture-prose.md).

## Context

`GRAPHICS-011` established `Extrinsic.Graphics.SpatialDebugVisualizers` as the data-only graphics-side packet builder that turns input records (bounds, hierarchy nodes, split planes, convex-hull wire edges, point markers) into transient debug packets with deterministic limits and diagnostics. The packet contract is **frozen** by `GRAPHICS-011` — no new input record types or new diagnostic fields are added without an explicit follow-up.

`GRAPHICS-011Q` answered the producer-side questions that `GRAPHICS-011` deferred: where the concrete adapters that translate geometry-tree outputs into those data-only input records actually live, how they are named, how they interact with the canonical truncation budget, where their diagnostics report, and where their tests sit. The naïve "graphics imports geometry trees" answer would invert the `geometry -> core` layer rule from `AGENTS.md` §2 and copy the legacy `src/legacy/Graphics/Graphics.{BVH,KDTree,Octree,ConvexHull}DebugDraw` pattern into the promoted graphics layer. The naïve "geometry imports graphics packets" answer is symmetrically forbidden.

This ADR captures the runtime-adapter answer as the canonical durable home. `docs/architecture/graphics.md` keeps a short canonical summary of the `Extrinsic.Graphics.SpatialDebugVisualizers` seam (data-only input records, transient debug packets, deterministic limits, diagnostics) and retains a single pointer line to this ADR for the adapter ownership, naming, pre-filter / budget policy, diagnostics handoff, and test placement.

## Decision

### 1. Concrete adapter ownership

Concrete adapters that translate `Geometry::BVH`, `Geometry::KDTree`, `Geometry::Octree`, and convex-hull outputs into the data-only `Extrinsic.Graphics.SpatialDebugVisualizers` input records (`SpatialDebugAabb`, `SpatialDebugHierarchyNode`, `SpatialDebugSplitPlane`, `SpatialDebugWireEdge`, plus point markers) live in **runtime extraction**, not in `src/geometry` and not in `src/graphics`.

Runtime is the only layer that may import **both**:

- The geometry tree implementations (`Geometry.BVH` / `Geometry.KDTree` / `Geometry.Octree` / `Geometry.ConvexHull`).
- The graphics packet types (`Extrinsic.Graphics.SpatialDebugVisualizers`).

Allowing `geometry` to reference graphics packet structs would invert the `geometry -> core` layer rule in `AGENTS.md` §2. Allowing graphics to import geometry tree implementations would couple graphics to geometry algorithms (the same anti-pattern as the legacy `src/legacy/Graphics/Graphics.{BVH,KDTree,Octree,ConvexHull}DebugDraw` modules).

Editor / app code may still own user-facing toggles (enable/disable, color choice, leaf/internal filters, per-depth filtering) but **must** funnel them into the runtime adapter as inputs; it must not call into `Extrinsic.Graphics.SpatialDebugVisualizers` directly with live geometry references.

### 2. Adapter naming and module placement

Concrete adapter modules land next to the existing runtime extraction in `src/runtime/` under the `Extrinsic.Runtime.SpatialDebugAdapters` umbrella module name.

Initial expected source files:

- `Runtime.SpatialDebugAdapters.cppm` (umbrella).
- Per-structure helpers — `Runtime.SpatialDebugAdapters.BVH` / `.KDTree` / `.Octree` / `.ConvexHull` — if they are split for build-time isolation.

Public adapter functions follow the verb-style `Build*SpatialDebugInputs(...)` naming already used by other runtime extraction helpers. Example shape:

```
BuildBVHSpatialDebugInputs(const Geometry::BVH&,
                           SpatialDebugBVHAdapterOptions,
                           ...) -> /* data-only record spans/vectors */
```

The returned spans/vectors feed `Extrinsic.Graphics.SpatialDebugVisualizers::BuildSpatialDebugPackets`.

The `Extrinsic.Graphics.SpatialDebugVisualizers` packet contract itself remains **frozen** by this clarification: input record types and the diagnostics struct must not grow new fields to accommodate adapter-specific knowledge. New adapter inputs route through the existing bounds / hierarchy-node / split-plane / wire-edge / point-marker shapes.

### 3. Output limit and pre-filter policy

The canonical truncation budget stays graphics-owned:

- `SpatialDebugVisualizerOptions::MaxLinePackets`
- `SpatialDebugVisualizerOptions::MaxPointPackets`
- `SpatialDebugVisualizerOptions::MaxDepth`

The graphics packet builder is the **single place** that enforces budget truncation. `TruncatedLineBudget` / `TruncatedPointBudget` / `RejectedDepthLimitCount` continue to be reported by graphics. Adapters must not bypass or duplicate this budget.

Adapters may apply CPU-side **pre-filters** before emitting input records:

- Leaf-only.
- Internal-only.
- Occupancy-only.
- Per-depth subsetting.
- Capped traversal depth.

Pre-filtered records are simply not produced and never reach graphics. Editor / app-side toggles (`Overlay`, `ColorByDepth`, `LeafOnly`, `DrawInternal`, `OccupiedOnly`) from the legacy debug-draw modules map to either adapter pre-filter options or to the existing `SpatialDebugVisualizerOptions` color / depth fields — they must not introduce new graphics-side options.

### 4. Diagnostics handoff

`SpatialDebugVisualizerDiagnostics` remains the **single graphics-side diagnostic surface** for input-record validity, emitted line / point counts, and rejection / truncation reasons:

- `RejectedInvalidBoundsCount`.
- `RejectedInvalidCoordinateCount`.
- `RejectedDepthLimitCount`.
- `RejectedTopologyCount`.
- `TruncatedLineBudget`.
- `TruncatedPointBudget`.

Adapters do **not** introduce a parallel graphics-visible diagnostics struct. Runtime-side adapter invocation counts, snapshot-construction CPU cost, and pre-filter rejection counts (e.g. nodes filtered by leaf-only or depth pre-filter) report through `RuntimeRenderExtractionStats` (or a dedicated runtime-owned spatial-debug stats sibling) and stay outside `Extrinsic.Graphics.SpatialDebugVisualizers`.

Graphics never inspects runtime-side adapter diagnostics. Runtime is responsible for forwarding adapter-visible failure modes (missing / empty geometry tree, null adapter options) through its own diagnostics rather than fabricating invalid input records to trigger graphics-side rejection counters.

### 5. Adapter test placement

Adapter tests are **runtime integration tests** under `tests/integration/runtime/` (matching the existing `Test.RuntimeRenderExtraction.cpp` placement), because the adapters bridge two layers (geometry source structures + the graphics packet contract) and verifying both ends in the same test is the canonical shape.

Pure geometry helpers used by adapters keep their tests under `tests/unit/geometry/`. The data-only graphics packet contract keeps its tests under `tests/unit/graphics/Test.Graphics.SpatialDebugVisualizers.cpp`.

Editor / app-only adapter wiring (UI toggles, controllers) is covered, if at all, by app-level tests / sandboxes; it must not be folded into the graphics or geometry unit suites.

New runtime adapter test files use the `Test.<Name>.cpp` naming (`AGENTS.md` §7: "New C++ test files use `Test.<Name>.cpp`"). Test labels follow `integration;runtime;graphics` so the default CPU gate (`-LE 'gpu|vulkan|slow|flaky-quarantine'`) still exercises adapter integration coverage without requiring Vulkan.

## Consequences

Positive:

- Geometry keeps its `geometry -> core` layer rule clean; the legacy `Graphics.{BVH,KDTree,Octree,ConvexHull}DebugDraw` anti-pattern does not return in promoted form.
- The graphics packet contract is frozen, so editor / runtime / geometry feature work cannot grow new graphics fields under cover of an adapter change.
- A single graphics-side diagnostics surface plus a single graphics-side truncation budget means consumers cannot drift on what "the budget was hit" or "the input was rejected" means.
- Adapter integration tests run under the default CPU gate (`integration;runtime;graphics`), so adapter regressions surface without needing a GPU host.

Trade-offs and risks:

- Editor toggles that previously poked directly at graphics options now must round-trip through the runtime adapter. This is a deliberate cost of the layering rule; editors that work around it (e.g. by stashing graphics options in a sidecar) are violating §1 and should be rejected in review.
- The `Extrinsic.Runtime.SpatialDebugAdapters` umbrella is **planned** — no concrete module exists yet. The naming and module-placement decision in §2 must be respected when the umbrella lands; if a different name is chosen first, this ADR must be amended rather than silently superseded.
- The pre-filter / truncation split makes adapter authors responsible for keeping their pre-filter counts and graphics' truncation counts coherent — for example, the adapter must not silently feed truncated records back through a second pass that would also be counted against `TruncatedLineBudget`. Reviewers must check that adapters report pre-filter rejections only through `RuntimeRenderExtractionStats` (or sibling).

Follow-up tasks required: none from this ADR. The concrete `Extrinsic.Runtime.SpatialDebugAdapters` module(s) land as a future runtime task under `tasks/backlog/runtime/`; no graphics-side change is required.

## Alternatives Considered

- **Graphics imports geometry trees directly.** Rejected per §1: inverts the `geometry -> core` layer rule and copies the legacy `Graphics.*DebugDraw` anti-pattern into the promoted layer.
- **Geometry exposes graphics packet structs.** Rejected per §1: symmetric violation of layering; would couple geometry algorithms to a graphics-side packet contract.
- **Adapter-specific diagnostic fields on `SpatialDebugVisualizerDiagnostics`.** Rejected per §§2, 4: the packet contract is frozen, and adapter-side counts belong on runtime extraction stats so multiple adapters do not contend for the same graphics field.
- **Adapter pre-filters duplicated as new `SpatialDebugVisualizerOptions` fields.** Rejected per §3: would grow the graphics option surface per adapter and force the graphics builder to re-implement filters the adapter already applied.
- **Adapter tests under `tests/unit/graphics/` to keep all spatial-debug tests in one place.** Rejected per §5: a unit-test directory is the wrong layer for a two-sided bridge; integration tests under `tests/integration/runtime/` validate both ends with one assertion shape.

## Validation

- [`tasks/done/GRAPHICS-011`](../../tasks/archive/GRAPHICS-011-spatial-debug-visualizers.md) records the underlying `Extrinsic.Graphics.SpatialDebugVisualizers` packet contract and the canonical truncation budget / diagnostics surface that this ADR keeps frozen.
- [`tasks/done/GRAPHICS-011Q`](../../tasks/archive/GRAPHICS-011Q-spatial-debug-adapter-clarifications.md) records the five clarification decisions captured in §§1–5.
- `src/graphics/renderer/README.md` carries the matching spatial-debug-visualizer bullet authored by `GRAPHICS-011Q`.
- The default CPU correctness gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) exercises the data-only packet contract under `tests/unit/graphics/` and any runtime-side adapter integration tests under `tests/integration/runtime/` without requiring a Vulkan device.
