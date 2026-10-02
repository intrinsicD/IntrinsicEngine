---
id: RUNTIME-319
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence]
---
# RUNTIME-319 — Persist processed geometry properties in scene documents

## Goal
- A scene save/load round trip keeps every typed property an entity carries on
  every element domain, not only the canonical geometry streams, so authored
  state that names a processed property (attribute bindings, Color overlays,
  presentation slots, vector fields) resolves after reload.

## Non-goals
- No new file format for geometry or property tables (GEOIO-005/RUNTIME-283
  own PLY/CSV/NPY property IO); no GPU-only ring fronts (only CPU-published
  properties persist).

## Context
- Found in RUNTIME-315 slice 4 (2026-10-02): `Runtime.SceneSerialization.cpp`
  writes only positions, normals, texcoords, corner normals/texcoords and
  topology (`AddVertices`/`AddNodes`/...). A binding to a processed property
  therefore loads stale: RUNTIME-315 keeps it, draws the default, counts it in
  `SceneSerializationStats::StaleAttributeBindings` and logs one warning per
  load. RUNTIME-284 lists the same gap for checkpoints.
- Reuse the typed property snapshots (`GeometryScalarPropertySnapshot`,
  `DetectGeometryPropertyValueKind`) and the property-domain vocabulary; skip
  topology/deletion rows (`IsTopologyProperty`).

## Control surfaces
- Config/scene: the scene document gains a per-domain typed property table;
  current-format round trip only (AGENTS.md §5).
- UI/agent: none new.

## Slice plan
1. **Write/read typed properties (~350 lines).** Bool/int/uint/float/double/vec2-4
  per domain with count validation bounded by the domain size, finite-value
  rules per kind, fail-closed parsing; stats count persisted properties.
2. **Round-trip tests (~200 lines).** Mesh, graph and point cloud with
  processed properties on every domain; an attribute binding to a processed
  property resolves after reload (`StaleAttributeBindings == 0`).

## Acceptance criteria
- [ ] Every non-topology typed property survives save/load bit-exactly on all eight element domains.
- [ ] Malformed or oversized property tables reject the document without partial mutation.
- [ ] RUNTIME-315 bindings to processed properties resolve after reload.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeSceneSerialization|VertexChannelBindings' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```
