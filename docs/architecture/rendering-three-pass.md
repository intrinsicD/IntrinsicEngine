# Rendering Three-Pass Architecture (Canonical)

This is the canonical rendering architecture specification for the runtime path currently in use.

## Scope

The renderer uses three primitive-owned passes:

1. `SurfacePass` (filled triangles)
2. `LinePass` (edges/wire/debug lines)
3. `PointPass` (points/nodes/point clouds/debug points)

These primitive-owned passes feed later composition/overlay stages. There are no parallel legacy collector passes.

## Core Invariants

- Primitive ownership is explicit: surfaces/lines/points are rendered only by their owning pass.
- ECS toggle model is presence/absence of typed components (`ECS::Surface::Component`, `ECS::Line::Component`, `ECS::Point::Component`).
- CPU geometry authority is PropertySet-backed (`PointCloud::Cloud`, `Graph`, `Halfedge::Mesh`).
- GPU rendering is BDA-driven from shared `GeometryGpuData` buffers.
- Mesh/graph/point-cloud paths are equal peers in lifecycle, upload, and scheduling.

## Pass Contract

| Pass | Retained Source | Transient Source | Notes |
|------|------------------|------------------|-------|
| `SurfacePass` | `ECS::Surface::Component` + `GeometryGpuData` | `SubmitTriangle` buffer | Per-face attributes via face attribute BDA channel |
| `LinePass` | `ECS::Line::Component` (`Geometry` + `EdgeView`) | `DebugDraw::GetLines()` / overlays | Edge topology sourced from PropertySets |
| `PointPass` | `ECS::Point::Component` | `DebugDraw::GetPoints()` | Mode pipelines: FlatDisc, Surfel (future: EWA/Gaussian variants) |
| `PostProcessPass` | `SceneColor` HDR intermediate | — | Tone mapping + optional FXAA, writes LDR presentation target |

## Data Contract (CPU -> GPU)

All renderable buffers are derived from PropertySet spans (`std::span`) and uploaded through lifecycle/sync systems.

- Vertex-domain data -> positions/normals/aux buffers
- Edge-domain data -> edge index + optional per-edge aux
- Face-domain data -> optional per-face aux

Dirty domains drive sync granularity:

- `VertexPositions`
- `VertexAttributes`
- `EdgeTopology`
- `EdgeAttributes`
- `FaceTopology`
- `FaceAttributes`

Position/topology changes may escalate to full re-upload; pure attribute changes use incremental extraction/upload.

## Pipeline Order

`DefaultPipeline` execution order:

1. `PickingPass`
2. `SurfacePass`
3. `LinePass`
4. `PointPass`
5. `PostProcessPass`
6. `SelectionOutlinePass`
7. `DebugViewPass`
8. `ImGuiPass`

## Robustness Requirements

- Reject non-finite positions/normals on submission/upload paths.
- Skip degenerate triangles.
- Clamp line widths and point radii to safe ranges.
- Condition EWA covariance (when active) and fall back safely if ill-conditioned.
- Keep push constants within device limits (compile/runtime checks).

## Performance Intent

- CPU frame contribution target for rendering systems: under 2 ms.
- Retained rendering avoids per-frame geometry rebuilds.
- Transient paths use per-frame host-visible buffers with bounded growth and telemetry overflow counters.

## Where Active Work Lives

- Near-term execution queue: `TODO.md` (`Now / Next / Later / Planned`).
- Medium/long-horizon planning: `ROADMAP.md`.
- Historical migration narrative: `PLAN.md` (archival index).
