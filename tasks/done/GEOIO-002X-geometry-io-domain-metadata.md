# GEOIO-002X — Geometry IO format domain metadata

## Goal
- Add geometry-owned metadata that lets downstream asset/runtime routing identify supported import/export domains for promoted geometry IO formats without importing assets/runtime/graphics into `src/geometry`.

## Non-goals
- No file IO dispatcher, byte transport, asset registry integration, runtime scene ingestion, ECS construction, or GPU upload work.
- No codec behavior changes for OBJ/OFF/STL/PLY/XYZ/PTS/XYZRGB/PCD/TGF/edge-list parsing or writing.
- No GLTF/GLB ownership or model/material/texture ingest.
- No mechanical legacy deletion.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `main` local workspace.
- Parent task: `tasks/done/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002` requires domain-selection metadata needed by asset/runtime import routing while preserving geometry's independence from assets/runtime/graphics.
- `ASSETIO-001` remains responsible for byte transport, asset payload registration, ambiguity policy, and higher-layer routing decisions.

## Required changes
- [x] `src/geometry/Geometry.IO.cppm`:
  - [x] Add `Geometry::IO::GeometryIODomain` for mesh, point-cloud, and graph domains.
  - [x] Add `Geometry::IO::GeometryIOFormatKind` and `GeometryIOFormatInfo` for supported promoted geometry formats.
  - [x] Add constexpr lookup helpers for supported formats, extension lookup, import-domain lookup, export-domain lookup, and import/export domain predicates.
  - [x] Keep extension matching deterministic, ASCII case-insensitive, and tolerant of leading `.` prefixes.
- [x] `src/geometry/CMakeLists.txt` and `src/geometry/Geometry.cppm`:
  - [x] Register and umbrella-export `Geometry.IO`.
- [x] `tests/unit/geometry/Test.GeometryIO.cpp`:
  - [x] Add metadata coverage for mesh, point-cloud, graph, binary-capability, unsupported-domain, and unknown-extension cases.

## Tests
- [x] Added `GeometryIO_Metadata.ReportsSupportedImportAndExportDomains` under existing `unit;geometry` GeometryIO coverage.

## Docs
- [x] Updated `docs/migration/nonlegacy-parity-matrix.md` to record `GEOIO-002X` as geometry-owned IO domain metadata evidence and to preserve asset/runtime ownership of routing orchestration.
- [x] Regenerated `docs/api/generated/module_inventory.md` after adding the public `Geometry.IO` module.

## Acceptance criteria
- [x] `Geometry.IO` exposes supported import/export domains for OBJ, OFF, STL, PLY, XYZ, PTS, XYZRGB, PCD, TGF, and edge-list formats.
- [x] OFF reports mesh import support but no geometry-owned export support.
- [x] PLY reports both mesh and point-cloud import/export support.
- [x] PTS and XYZRGB report point-cloud import support only; the geometry-owned text exporter remains the canonical XYZ writer.
- [x] Unknown or empty extensions report no domains.
- [x] `src/geometry/*` imports only allowed lower-layer dependencies and remains independent of assets/runtime/graphics.
- [x] The module inventory and migration matrix are synchronized.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding assets/runtime/graphics/RHI imports to `src/geometry/*`.
- Adding a generic geometry file dispatcher or registry that owns byte transport.
- Changing existing importer/exporter parsing or writing behavior.
- Changing asset/runtime routing policy or extension ambiguity resolution.
- Mixing mechanical legacy deletion with semantic IO metadata implementation.

## Completion
- Completed: 2026-05-11.
- Status: done.
- Implementation commit: pending in workspace.
- Verified in this session:
  - `cmake --preset ci` — failed before configure because this host does not have the preset-required `clang-20` / `clang++-20` on `PATH`; therefore `cmake --build --preset ci --target IntrinsicTests` and the `build/ci` CTest gate could not run locally in this session.
  - Supplemental focused evidence: existing non-default `cmake-build-debug` cache uses `/usr/bin/clang++-22` and `/usr/bin/clang-22`.
  - Supplemental focused evidence: `cmake --build cmake-build-debug --target IntrinsicGeometryTests` succeeded.
  - Supplemental focused evidence: `/home/alex/.local/share/JetBrains/Toolbox/apps/clion/bin/cmake/linux/x64/bin/ctest --test-dir cmake-build-debug --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed 131/131 focused `GeometryIO` tests.
  - `python3 tools/repo/check_layering.py --root src --strict` — no layering violations found.
  - `python3 tools/repo/check_test_layout.py --root . --strict` — 0 findings.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` — 421 modules; inventory updated.
  - `python3 tools/agents/check_task_policy.py --root . --strict` — 153 task files validated; 0 findings.
  - `python3 tools/docs/check_doc_links.py --root .` — 255 relative links checked; no broken relative links found.
  - `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main` — docs sync rules satisfied.
- Verification still required on a CI host with the repository default C++23 toolchain:
  - `cmake --preset ci`
  - `cmake --build --preset ci --target IntrinsicTests`
  - `ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- Notes:
  - The metadata intentionally reports geometry codec capabilities only. Asset/runtime layers still own actual routing, payload registration, file-source policy, and ambiguous extension decisions.



