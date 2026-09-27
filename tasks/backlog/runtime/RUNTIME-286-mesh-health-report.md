---
id: RUNTIME-286
theme: F
depends_on: [GEOM-110, RUNTIME-287]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources, geometry.property-coherence]
---
# RUNTIME-286 — Mesh health report

## Goal
- Report topology, defects, scale and quality of a mesh entity in one read-only
  operation, optionally publishing problem-marker properties.

## Non-goals
- No mesh repair; no new analysis kernels beyond GEOM-110.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Geometry already has, unused by runtime: `Geometry.HalfedgeMesh.Analysis` (`Analyze`, issue masks, `v:analysis_problem` markers), `Geometry.HalfedgeMesh.Quality` (`ComputeQuality`, `ComputeDistributions`), `Geometry.HalfedgeMesh.Utils`, `Geometry.AABB`; components/genus from GEOM-110. Detached mesh through `Runtime.GeometryProcessingOperations.MeshSupport.hpp` (`Geometry.Mesh.Conversion::ToHalfedgeMesh`).
- Large meshes run as a CPU `JobService` job with a `Pending` result, like curvature.

## Control surfaces
- Config: N/A (per-call parameters).
- UI: Mesh Health window (UI-066).
- Agent/CLI: `mesh_health {entity, publish_markers:false}` (read-only unless markers are published) in `Runtime.AgentOperations`.

## Required changes
- [ ] `Runtime.MeshHealthOperations.cppm` + implementation: `ComputeEditorMeshHealthReport(commands, entity, MeshHealthParams)` → counts, components with per-component topology, boundary loops and lengths, non-manifold vertex/edge counts (add an edge count to `Analyze` if missing), isolated vertices, degenerate/skinny/non-triangle faces, non-finite counts, AABB and diagonal, total area, volume if closed, edge-length and angle stats.
- [ ] `publishMarkers` publishes the `MeshAnalysis` Bool marker properties through history ("Publish mesh health markers").
- [ ] Agent operation registered.

## Tests
- [ ] Contract tests on `tests/support/geometry` fixtures (closed sphere, torus, open disk, non-manifold fan, degenerate faces): exact counts; markers published on the vertex/face domains and undoable.

## Docs
- [ ] `docs/architecture/sandbox-editor-feature-boundaries.md`; module inventory regenerated.

## Acceptance criteria
- [ ] Report values match geometry-layer results on the fixtures; read-only by default (no history change).
- [ ] Marker publication is optional, undoable and on the originating domains.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'MeshHealth|HalfedgeMeshAnalysis|AgentOperations' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Reimplementing analysis/quality metrics in runtime.
