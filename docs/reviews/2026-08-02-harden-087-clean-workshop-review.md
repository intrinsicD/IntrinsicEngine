# Clean-workshop review — HARDEN-087 unified geometry element sources

## Change under review

- Change: retire
  [`HARDEN-087`](../../tasks/done/HARDEN-087-unified-geometry-element-source-components.md)
  after replacing graph-only `Nodes` storage with the canonical `Vertices`,
  `Halfedges`, and `Edges` element sources and migrating runtime consumers to
  capability-plus-provenance queries.
- Trigger(s): changes an exported ECS source vocabulary and runtime method,
  extraction, serialization, history, and editor-model bindings.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `tools/ci/run_clean_workshop_review.sh . --strict` scanned 735 source files, 6,917 import/include references, and 85 CMake links with no violation or allowlist entry. ECS retains only its explicitly required geometry property/connectivity types; runtime owns composition. |
| 2 | CMake target links match layer policy | pass | No target or `target_link_libraries(...)` edge changed. The graph garbage-collection correction remains in geometry, source ownership remains in ECS, and all consumer wiring remains in runtime. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | The exported ECS surface removes `Nodes` and exposes the same owned `PropertySet`-based element sources already used for mesh and point-cloud data. It names no runtime, graphics, platform, app, or live-service type. |
| 4 | Renderer member/subsystem growth is justified by an owning seam | n/a | No renderer member, graphics subsystem, GPU residency seam, or ownership rule changed. |
| 5 | New passes use typed IDs, not string routing | n/a | No frame-graph pass or renderer command route changed. |
| 6 | New frame-recipe dependencies resource-driven or explicitly justified | n/a | No frame recipe, resource declaration, or ordering edge changed. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | The task closes at `Operational`: exact element matrices, graph connectivity, custom properties, compaction, runtime bindings, scene round-trip, focused contracts, the complete CPU gate, and both required sanitizer gates are covered. No maturity follow-up is owed. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No alias, converter, duplicate `Nodes` mirror, compatibility facade, allowlist row, warning-mode gate, or temporary exception was added. |

## Architecture checklist result

- Physical element capability and geometry provenance remain independent:
  point clouds publish `Vertices`; graphs publish `Vertices`, `Halfedges`, and
  `Edges`; meshes add `Faces`.
- Runtime UI keeps logical `GraphNode` terminology while resolving the shared
  physical vertex source through the same availability/apply paths as other
  domains.
- `Geometry::Graph::GarbageCollection` owns its remap invariant before ECS
  materialization; ECS does not compensate for invalid geometry connectivity.
- No dependency edge, backend axis, config-only path, byte layout, GPU claim,
  or performance claim was introduced.

## Findings → follow-ups

- No findings.
