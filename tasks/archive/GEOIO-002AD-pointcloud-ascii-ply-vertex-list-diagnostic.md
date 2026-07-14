# GEOIO-002AD — Point-cloud ASCII PLY vertex-list diagnostic

## Goal
- Harden `Geometry::PointCloudIO::LoadPLY` so ASCII PLY point-cloud files reject unsupported list properties on the `vertex` element instead of silently accepting malformed per-vertex rows.

## Non-goals
- No new point-cloud attribute storage for variable-length per-vertex payloads.
- No changes to mesh PLY import/export behavior.
- No asset/runtime/graphics routing or registry work.

## Context
- Owner: `geometry -> core` only.
- `GEOIO-002` tracks promoted geometry-owned IO parity and deterministic diagnostics for unsupported PLY variants.
- Binary point-cloud PLY already rejects list properties on the `vertex` element; this task aligns the ASCII path with that contract.

## Required changes
- [x] Reject ASCII point-cloud PLY `vertex` list properties in `src/geometry/Geometry.PointCloud.IO.cpp`.
- [x] Preserve existing scalar ASCII point-cloud PLY import behavior.

## Tests
- [x] Add a `unit;geometry` negative-path test under `tests/unit/geometry` for ASCII point-cloud PLY vertex list properties.
- [x] Run focused Geometry IO tests for the new negative path.
- [x] Run the full CPU-supported `GeometryIO_` subset available in the local compatible build tree.

## Docs
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` with the new point-cloud PLY diagnostic evidence.

## Acceptance criteria
- [x] Unsupported ASCII PLY point-cloud vertex list properties return `Core::ErrorCode::InvalidFormat`.
- [x] Existing ASCII PLY point-cloud scalar import tests continue to pass.
- [x] `src/geometry/*` remains independent of assets/runtime/graphics.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryIO_' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Completed verification:
- `cmake --build cmake-build-debug --target IntrinsicGeometryTests` — passed as supplemental verification using the existing clang-22 / clang-scan-deps-22 debug build tree.
- `ctest --test-dir cmake-build-debug --output-on-failure -R 'GeometryIO_PointCloudIO\.LoadAsciiPLYPointCloudRejectsListPropertyInVertex' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — passed, 1/1.
- `ctest --test-dir cmake-build-debug --output-on-failure -R 'GeometryIO_' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — passed, 146/146.
- `python3 tools/repo/check_layering.py --root src --strict` — passed.
- `python3 tools/repo/check_test_layout.py --root . --strict` — passed.
- `python3 tools/docs/check_doc_links.py --root .` — passed in warning mode with no broken relative links.
- `python3 tools/agents/check_task_policy.py --root . --strict` — passed before retirement; re-run after retirement once completion metadata was added.

Blocked verification:
- `cmake --preset ci` could not complete because this shell cannot find the preset-pinned `clang-20`/`clang++-20` binaries on `PATH`.

## Forbidden changes
- Adding assets/runtime/graphics/RHI imports to `src/geometry/*`.
- Mixing mechanical moves or legacy deletion with semantic IO diagnostics.
- Introducing point-cloud variable-length attribute APIs.

## Completion
- Completed: 2026-05-13.
- Status: done.
- Implementation commit: this local change (`GEOIO-002AD: reject ASCII PLY point vertex lists`).

