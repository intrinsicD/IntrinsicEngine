# GEOIO-002AC — Geometry OBJ face attribute index import

## Goal
- Harden geometry-owned OBJ mesh import so `Geometry::MeshIO::LoadOBJ` preserves face-referenced `vt` and `vn` attributes when OBJ face tokens use independent `v/vt/vn`, `v/vt`, or `v//vn` indices.

## Non-goals
- No OBJ material, texture file, smoothing group, line primitive, or per-corner property API work.
- No asset/runtime/graphics import routing.
- No OBJ exporter changes.
- No mechanical legacy cleanup.

## Context
- Owner: `geometry -> core` only.
- Parent task: `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
- Prior slices added lockstep OBJ vertex normal and texcoord import/export. Common OBJ files store positions, texcoords, and normals in separate arrays and reference them from face tokens.
- The promoted geometry result can represent these as duplicated vertices where distinct face attribute tuples reference the same position with different attributes.

## Required changes
- [x] Update `src/geometry/Geometry.HalfedgeMesh.IO.cpp` OBJ import parsing to resolve optional face texcoord and normal indices, including negative relative indices.
- [x] Remap distinct `(position, texcoord, normal)` tuples into promoted vertex rows when face attributes are used.
- [x] Preserve current position-only OBJ import behavior and existing lockstep property fallback when faces do not reference attributes.

## Tests
- [x] Add `unit;geometry` coverage for non-lockstep OBJ texcoord face indices that duplicate a shared position with different texcoords.
- [x] Add `unit;geometry` coverage for OBJ normal face indices when the normal array is not lockstep with positions.
- [x] Add negative-index coverage for face-referenced OBJ texcoords/normals.

## Docs
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` with the completed hardening slice.
- [x] Do not regenerate module inventory because no module surface is added, removed, or renamed.

## Acceptance criteria
- [x] OBJ face tokens with valid independent `vt`/`vn` indices populate `v:texcoord` and `v:normal` after remapping.
- [x] Malformed or out-of-range face attribute indices return `Core::ErrorCode::InvalidFormat`.
- [x] `src/geometry/*` dependency boundaries remain unchanged.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryIO_MeshIO\.LoadsOBJ.*Face.*Indices|GeometryIO_MeshIO\.LoadOBJRejectsOutOfRangeFaceTexcoordIndex' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

Completed verification:
- `python3 tools/repo/check_layering.py --root src --strict` — passed.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed before retirement; re-run after retirement once completion metadata was added.
- `python3 tools/docs/check_doc_links.py --root .` — passed in warning mode with no broken relative links.
- `cmake --build cmake-build-debug --target IntrinsicGeometryTests` — passed as supplemental verification using the existing clang-22 debug build tree.
- `ctest --test-dir cmake-build-debug --output-on-failure -R 'GeometryIO_MeshIO\.(LoadsOBJFace.*Indices|LoadOBJRejectsOutOfRangeFaceTexcoordIndex)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — passed, 4/4.
- `ctest --test-dir cmake-build-debug --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — passed, 145/145.

Blocked verification:
- `cmake --preset ci` could not complete because this shell cannot find the preset-pinned `clang-20`/`clang++-20` binaries on `PATH`.

## Forbidden changes
- Adding assets/runtime/graphics/RHI imports to `src/geometry/*`.
- Changing public `Geometry::MeshIO` signatures or status enums.
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

## Completion
- Completed: 2026-05-13.
- Status: done.
- Implementation commit: this local change (`GEOIO-002AC: import OBJ face attribute indices`).
