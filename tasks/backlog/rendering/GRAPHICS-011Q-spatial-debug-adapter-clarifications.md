# GRAPHICS-011Q — Spatial debug adapter clarification follow-ups

## Goal
- Clarify which higher-layer adapters should translate concrete geometry/runtime spatial structures into the data-only `Extrinsic.Graphics.SpatialDebugVisualizers` input records.

## Non-goals
- No C++ behavior changes.
- No new spatial data structure algorithms.
- No editor UI controller work.

## Context
- `GRAPHICS-011` introduced graphics-owned packet builders for bounds, hierarchy nodes, split planes, convex-hull wire edges, and point markers without importing live geometry trees, runtime, editor UI, or ECS ownership into graphics.
- Concrete adapters for `Geometry::BVH`, `Geometry::KDTree`, `Geometry::Octree`, convex-hull mesh outputs, and editor/debug tooling should live in the owning layer that already has access to those structures.

## Required changes
- Decide the owning layer for concrete BVH/KD-tree/octree/convex-hull adapters (geometry helper API, runtime extraction helper, or app/editor-only utility).
- Document adapter naming, output limit policy, and diagnostics handoff into `SpatialDebugVisualizerDiagnostics`.
- Clarify whether adapter tests belong under geometry unit tests, runtime integration tests, or app/editor tests.

## Tests
- Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- Update `docs/architecture/graphics.md` and any geometry/runtime architecture docs touched by the adapter ownership decision.

## Acceptance criteria
- Concrete adapter ownership is clear without adding prohibited graphics dependencies.
- Future adapter work can proceed without changing the graphics packet-builder contract.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Importing runtime/editor/ECS ownership into `src/graphics`.
- Adding `src/graphics` dependencies on geometry implementation internals.

