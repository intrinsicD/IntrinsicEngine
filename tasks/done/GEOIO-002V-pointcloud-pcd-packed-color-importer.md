# GEOIO-002V — Geometry-owned PCD packed color import parity

## Goal
- Extend `Geometry::PointCloudIO::LoadPCD` to import common packed `rgb` and `rgba` PCD color fields in both ASCII and binary encodings, closing the point-cloud color parity gap left after separate `r g b` field support.

## Non-goals
- No `binary_compressed` / LZF PCD decompression in this slice.
- No PCD writer format change; `WritePCD` and `WritePCDBinary` continue emitting separate `r g b` fields for geometry-owned exports.
- No new public module surface or status enum values.
- No assets/runtime/graphics ownership of point-cloud file IO.
- No changes to PLY, XYZ/PTS/XYZRGB, OBJ/OFF/STL, or graph IO behavior.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `main` local workspace.
- Parent task: `tasks/done/GEOIO-002-geometry-io-parity-hardening.md`.
- Prior `GEOIO-002` slices added PCD ASCII/binary point-cloud import/export for separate `r g b` fields and recorded packed `rgb`/`rgba` PCD as remaining scope.
- Common PCD producers, including PCL-style files, encode color as a single 32-bit `rgb` or `rgba` scalar whose bits are `0xAARRGGBB`/`0x00RRGGBB`; ASCII files often print the same bits through a `float` field.

## Required changes
- [x] `src/geometry/Geometry.PointCloud.IO.cpp`:
  - [x] Add helpers that recognize supported packed PCD color fields (`COUNT 1`, `SIZE 4`, `TYPE F|I|U`).
  - [x] Decode packed `rgb` as opaque `0x00RRGGBB` and packed `rgba` as `0xAARRGGBB` into the existing `glm::vec4` point color storage.
  - [x] Preserve existing separate `r g b` behavior and prefer separate channels when both channel styles are present.
  - [x] Preserve signed 32-bit `TYPE I` packed color fields as raw color bit patterns so high-bit alpha/channel values are not rejected.
  - [x] Reject malformed packed color payloads with the existing `Core::ErrorCode::InvalidFormat` path.
- [x] `tests/unit/geometry/Test.GeometryIO.cpp`:
  - [x] Add `unit;geometry` coverage for ASCII PCD `TYPE F` packed `rgb` import.
  - [x] Add `unit;geometry` coverage for ASCII PCD `TYPE I` packed `rgba` import with the high bit set.
  - [x] Add `unit;geometry` coverage for binary PCD `TYPE U` packed `rgba` import.

## Tests
- [x] Added `GeometryIO_PointCloudIO.LoadsASCIIPCDWithPackedFloatRgb`.
- [x] Added `GeometryIO_PointCloudIO.LoadsASCIIPCDWithSignedPackedRgba`.
- [x] Added `GeometryIO_PointCloudIO.LoadsBinaryPCDPointCloudWithPackedRgba`.
- [x] Existing PCD tests continue to cover separate `r g b` fields, normals, extra scalar skipping, width/height point count fallback, truncated binary bodies, and unsupported `binary_compressed` rejection.

## Docs
- [x] Updated `docs/migration/nonlegacy-parity-matrix.md` to record PCD packed `rgb`/`rgba` import parity under `GEOIO-002V`.
- [x] No `docs/api/generated/module_inventory.md` refresh required because no public module declarations, module files, or exported names changed.

## Acceptance criteria
- [x] ASCII PCD import decodes packed float `rgb` colors into normalized point colors.
- [x] Binary PCD import decodes packed integer `rgba` colors, including alpha, into normalized point colors.
- [x] Separate `r g b` field import remains unchanged and takes precedence if both representations are present.
- [x] `src/geometry/*` imports only allowed lower-layer dependencies and remains independent of assets/runtime/graphics.
- [x] The migration matrix records the new parity evidence.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding assets/runtime/graphics/RHI imports to `src/geometry/*`.
- Implementing PCD `binary_compressed` decompression in this slice.
- Changing PCD writer output layout from separate `r g b` fields.
- Adding new public geometry module API.
- Mixing mechanical legacy deletion with semantic IO implementation.

## Completion
- Completed: 2026-05-11.
- Status: done.
- Implementation commit: `c6ec48a`.
- Verified in this session:
  - `cmake --preset ci` — failed before configure because this host does not have the preset-required `clang-20` / `clang++-20` on `PATH`; therefore `cmake --build --preset ci --target IntrinsicTests` and the focused `GeometryIO` CTest command could not run locally in this session.
  - Supplemental focused evidence: existing non-default `cmake-build-debug` cache uses `/usr/bin/clang++-22`; `cmake --build cmake-build-debug --target IntrinsicGeometryTests` succeeded (`ninja: no work to do`, touched objects already current).
  - Supplemental focused evidence: `/home/alex/.local/share/JetBrains/Toolbox/apps/clion/bin/cmake/linux/x64/bin/ctest --test-dir cmake-build-debug --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed 129/129 tests, including `LoadsASCIIPCDWithPackedFloatRgb`, `LoadsASCIIPCDWithSignedPackedRgba`, and `LoadsBinaryPCDPointCloudWithPackedRgba`.
  - `python3 tools/repo/check_layering.py --root src --strict` — no layering violations found.
  - `python3 tools/repo/check_test_layout.py --root . --strict` — 0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` — 151 task files validated; 0 findings.
  - `python3 tools/docs/check_doc_links.py --root .` — 255 relative links checked; no broken relative links found.
- Verification still required on a CI host with the repository C++23 toolchain:
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- Notes:
  - `LoadPCD` now detects a packed color candidate only when separate `r g b` fields are absent. This avoids changing existing fixtures or files that already provide explicit channel fields.
  - Packed `rgb`/`rgba` import accepts 32-bit `F`, `I`, or `U` fields with `COUNT 1`; unsupported packed field shapes fail through the existing deterministic `InvalidFormat` path once color decoding is required.
  - Remaining `GEOIO-002` scope includes granular IO diagnostics, OBJ per-corner `p/t/n` vertex-duplication parity, and PCD `binary_compressed` LZF decompression.



