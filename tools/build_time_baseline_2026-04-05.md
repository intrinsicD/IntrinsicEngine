# Build-Time Baseline — Post-PImpl (2026-04-05)

Captured after completing Tier A + Tier B PImpl migrations (RenderOrchestrator, RenderDriver, GraphicsBackend, AssetPipeline, PipelineLibrary).

## Environment

| Parameter | Value |
|-----------|-------|
| Compiler | Clang 20.1.2 (Ubuntu) |
| Build system | Ninja + CMake |
| Build type | Debug |
| CPU cores | 4 |
| Parallelism | `-j4` |

## Build-Time Measurements

| Scenario | Wall time | User time | Steps | Notes |
|----------|-----------|-----------|-------|-------|
| **Clean build** (all targets incl. tests) | **11m 24s** | 42m 26s | 1219 | Full rebuild from scratch |
| **No-op incremental** | **0.125s** | 0.090s | 0 | Ninja overhead only |
| **Touch `.cpp` impl** (RenderOrchestrator) | **16s** | 16s | 8 | 1 object + 2 links; PImpl keeps cascade to minimum |
| **Touch `.cppm` interface** (RenderOrchestrator) | **3m 23s** | 6m 38s | 34 | Downstream importers must rebuild |
| **Touch `.cppm` interface** (RenderDriver) | **4m 19s** | 14m 38s | 81 | Wider fan-out (imported by more modules) |

## Key Insight

The primary PImpl benefit is that **implementation-file changes** (`.cpp`) no longer cascade. Touching `RenderOrchestrator.cpp` rebuilds only **1 object + links (16s)** vs touching the interface (34 objects, 3m23s). During normal development, most changes are implementation-only, making the 16s incremental the typical case.

## Module Fan-out Comparison (Interface Files)

Post-PImpl interface sizes vs pre-PImpl baseline (`tools/module_fanout_baseline_2026-04-03.md`):

| File | Lines (pre→post) | import (pre→post) | #include (pre→post) | export (pre→post) |
|------|-------------------|--------------------|-----------------------|--------------------|
| `RenderOrchestrator.cppm` | 139→138 (-1) | 9→20 (+11) | 5→5 (0) | 4→2 (-2) |
| `RenderDriver.cppm` | 130→144 (+14) | 24→24 (0) | 6→7 (+1) | 2→2 (0) |
| `GraphicsBackend.cppm` | 88→89 (+1) | 11→12 (+1) | 4→3 (-1) | 2→2 (0) |
| `AssetPipeline.cppm` | 85→86 (+1) | 3→3 (0) | 3→3 (0) | 2→2 (0) |
| `PipelineLibrary.cppm` | 82→92 (+10) | 7→7 (0) | 4→4 (0) | 2→2 (0) |

Note: RenderOrchestrator import count increased because new features were added concurrently with the PImpl migration. The export count reduction (4→2) reflects successful hiding of implementation types.

## Compile Hotspots (Top 10)

| Duration (s) | Source | Lines | Imports |
|-------------|--------|-------|---------|
| 48.1 | `Runtime.EditorUI.cppm` | 775 | 28 |
| 37.4 | `Graphics.Passes.Surface.cppm` | 358 | 11 |
| 34.8 | `Runtime.Engine.cppm` | 215 | 25 |
| 34.4 | `Graphics.Passes.HtexPatchPreview.cppm` | 145 | 12 |
| 30.7 | `Graphics.LifecycleUtils.cppm` | 175 | 6 |
| 27.3 | `Runtime.PointCloudKMeans.cppm` | 104 | 4 |
| 26.7 | `Graphics.RenderDriver.cppm` | 143 | 24 |
| 26.7 | `Graphics.Passes.SelectionOutline.cppm` | 86 | 10 |
| 26.2 | `main.cpp` | 2645 | 38 |
| 24.8 | `Graphics.Passes.Picking.cppm` | 70 | 8 |

Raw data: `tools/compile_hotspot_post_pimpl.json`

## Hot-Path Heap Churn Audit

All 5 PImpl'd classes confirmed clean:
- `m_Impl` allocated exactly once in constructor via `std::make_unique<Impl>()`
- Zero per-frame heap allocations through the Impl pointer
- Per-frame accessors are simple pointer dereferences
- Frame-scoped arena allocations (ScopeStack) are by design, not heap churn

## Test Validation

| Target | Tests | Result |
|--------|-------|--------|
| `IntrinsicCoreTests` | 304 | 302 passed, 2 skipped (`ArchitectureSLO.FrameGraphP95P99BudgetsAt2000Nodes`, `ArchitectureSLO.TaskSchedulerContentionAndWakeLatencyBudgets` — require high-resolution timing not available in container) |
| `IntrinsicGeometryTests` | 895 | 895 passed |
| `IntrinsicECSTests` | 59 | 59 passed |
| `IntrinsicTests` (non-GPU subset) | — | All non-GPU tests pass; GPU tests skipped (no Vulkan in container) |
