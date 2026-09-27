---
id: GEOIO-005
theme: F
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation, io.geometry-format-capabilities, geometry.element-domain-sources]
---
# GEOIO-005 — PLY arbitrary property attributes and CSV/NPY property-table IO

## Goal
- Round-trip arbitrary scalar/vector vertex and face properties through PLY, and
  read/write single-domain property tables as CSV and NPY, for property import/export
  (RUNTIME-283).

## Non-goals
- CSV/NPY are property-table formats, not geometry formats: do not add them to the geometry-format catalog.
- No runtime/UI code (RUNTIME-283, UI-063).

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- `Geometry.HalfedgeMesh.IO` (`MeshIOResult{Vertices, Halfedges, Edges, Faces PropertySets}`, `WritePLY/WritePLYBinary/WriteOBJ/WriteOFF/WriteSTL`); `WritePLY` emits only `v:position`, `v:normal`, `v:color` (`Geometry.HalfedgeMesh.IO.cpp`). `Geometry.PointCloud.IO` (`WritePLY/PCD/XYZ`). Shared PLY header parser from GEOIO-004. Capability table `src/core/Core.GeometryFormatCatalog.inc` (CSV is import-only, point-cloud-only).
- Follow the `intrinsicengine-geometry-io-format` slice template: parsers in an anonymous namespace, `Core::Expected<…Result>` readers, `*IOWriteStatus` writers, untrusted counts bounded against payload size before any reserve.

## Control surfaces
- Config: N/A.
- UI: via RUNTIME-283 / UI-063.
- Agent/CLI: via RUNTIME-283 `property_export`/`property_import`.

## Required changes
- [ ] Mesh and point-cloud PLY writers (ASCII and binary) emit all scalar and vector properties on vertices and faces (typed `float/double/int/uint/uchar`, vector components suffixed `_x/_y/_z/_w`), skipping `*:deleted` and structural topology; loaders round-trip them (verify current loader behavior first).
- [ ] `Geometry.Properties.TableIO`: `WritePropertyTableCsv/ReadPropertyTableCsv` (`index,<name>[_c]…` header, `std::to_chars` formatting) and `WritePropertyTableNpy/ReadPropertyTableNpy` (NPY v1.0, little-endian, `<f4/<f8/<i4/<u4/|b1`, shape `(rows,)` or `(rows, comps)`).

## Tests
- [ ] `tests/unit/geometry`: PLY round trip of extra vertex/face properties (ASCII + binary); CSV/NPY round trips per type; truncated, oversized-count, bad-header and non-finite fail-closed cases; deterministic output bytes.

## Docs
- [ ] `docs/architecture/geometry.md` IO section; `docs/architecture/assets.md` notes that property-table IO is outside the format catalog; module inventory regenerated.

## Acceptance criteria
- [ ] Arbitrary properties survive PLY round trips; CSV/NPY tables round-trip exactly for integer types and bit-exactly for floats.
- [ ] Untrusted inputs fail closed with explicit diagnostics.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryIO|PropertyTableIO|MeshIO|PointCloudIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding CSV/NPY to the geometry-format catalog; new third-party IO dependencies.
