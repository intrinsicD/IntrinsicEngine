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
- [ ] Every non-topology, non-derived typed property survives save/load bit-exactly on all eight element domains.
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

## Log
- 2026-10-02: Slice 1 (format, codec, tests). Decisions:
  - Scope: every typed property on all eight element domains except topology
    (`IsTopologyProperty`) and the streams a section already writes (positions,
    normals, texcoords, corner normals/texcoords, atlas labels). Nothing else is
    excluded, except engine-derived mirrors (`v:point`, `f:normal`;
    `IsEngineDerivedProperty`): they are not saved and not recreated on load,
    because no runtime consumer reads `v:point` and a saved copy goes stale. GPU-only ring fronts are not
    `PropertySet` entries and never reach the writer. No property-backed
    selection masks exist (selection is ECS state).
  - Value kinds: bool, int32, uint32, uint64, float, double, vec2/3/4. Strings
    are not a `PropertyValueKind`. A property whose type has no value kind
    (an opaque struct) is skipped at save, counted in
    `UnpersistedGeometryProperties` and logged.
  - Encoding: one `{name, kind, data}` entry per property in a per-section
    `properties` array. `data` is strict base64 of the little-endian element
    bytes. This is bit-exact, including inf and -0, which JSON numbers cannot
    carry, and costs about 1.33x the raw size. No compression or sidecar: the
    payload is linear in the element count the document already carries.
    Sidecars stay with GEOIO-005/RUNTIME-283 if they are ever needed. The base64
    codec moved from the agent capture code to `Extrinsic.Core.Base64`, with a
    strict decoder, and is shared, not duplicated. No GEOIO-005/RUNTIME-283
    typed codec exists yet to reuse.
  - Validation: the payload length must equal section count x element size
    before decoding, with an overflow guard. A duplicate, reserved or topology
    name, an unknown kind, a bool byte other than 0 or 1, or a NaN float rejects
    the whole document. The load stages into a fresh registry, so a rejected
    document never mutates the scene. Infinity is allowed because sentinel
    fields use it, matching `GeometryScalarNonfinitePolicy::AllowInfinity`.
    NaN is never a live output. The writer therefore skips a NaN-carrying
    property with a counted warning instead of failing the whole save.
  - Revisions: loaded properties are new storages, so their content revisions
    are fresh (`geometry.property-coherence`). Nothing carries over.
  - `kSceneDocumentVersion` is 4. Version 3 documents fail with
    `InvalidFormat` (AGENTS.md §5, no converter).
  - Bindings to processed properties now resolve after reload
    (`StaleAttributeBindings == 0`). A source that is absent from the document
    still loads as a counted fallback.
