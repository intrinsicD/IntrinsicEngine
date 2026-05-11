# GEOIO-002H — Geometry-owned XYZ/PTS/XYZRGB text point-cloud importer hardening

## Goal
- Harden `Geometry::PointCloudIO::LoadXYZ` to match the legacy
  `Graphics::XYZLoader` text-format parity surface for `.xyz`, `.pts`,
  `.xyzrgb`, and `.txt` point-cloud files. Specifically: accept
  `;`-delimited rows, skip `LH<digits>` scan-line marker lines, and
  recognize trailing-RGB color layout (`.xyzrgb`-style 7-or-more-token
  rows where colors are the last three tokens). Continue to honor the
  existing intensity-as-color path (4 tokens) and the canonical
  `x y z r g b`-at-position-3 path (6 tokens).

## Non-goals
- No new public module surface. `LoadXYZ` keeps the signature
  `Core::Expected<PointCloudIOResult> LoadXYZ(std::string_view absolute_path)`.
- No new helpers exposed from `Geometry.PointCloud.IO.cppm`; all new
  logic stays in the anonymous namespace inside the `.cpp`.
- No changes to mesh IO (`LoadOBJ`/`LoadOFF`/`LoadSTL`/mesh
  `LoadPLY`), point-cloud `LoadPLY`, `LoadPCD`, or graph `LoadTGF`.
- No granular `PointCloudIOReadStatus` diagnostics enum; failures
  continue to surface as `Core::ErrorCode::InvalidFormat`. The
  reader-side diagnostics enum across point-cloud loaders remains a
  separate follow-up under the parent task.
- No legacy `Graphics::XYZLoader` retirement or rewiring; that stays
  under `GRAPHICS-019` follow-ups.
- No asset registry, runtime scene ingestion, ECS construction, or
  GPU upload work.
- No GPU/Vulkan requirement in the default CPU gate.
- No additional vertex attributes (intensity-as-attribute, scan-line
  index, etc.) — only the existing `position`/optional `color` shape
  is populated, matching legacy `XYZLoader`.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-xwz1l`.
- Parent backlog task:
  `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`
  explicitly lists "XYZ/PTS/XYZRGB-style text point clouds" as part
  of the importer parity scope.
- Predecessor slices: `tasks/done/GEOIO-002F-pointcloud-ply-binary-importer.md`
  and `tasks/done/GEOIO-002G-pointcloud-pcd-binary-importer.md` shipped
  binary point-cloud parity (PLY, PCD). This slice closes the remaining
  point-cloud text-format parity gap.
- Behavioral reference:
  `src/legacy/Graphics/Importers/Graphics.Importers.XYZ.cpp`:
  - `s_Extensions` (line 25): `.xyz`, `.pts`, `.xyzrgb`, `.txt`.
  - `ParsePointColor` (lines 27-48): trailing-RGB at `tokens.size()-3`
    for 7+ tokens, RGB at offset 3 for 6 tokens, intensity at
    offset 3 for 4 tokens.
  - `IsScanLineMarker` (lines 50-65): `LH<digits>` single-token rows.
  - `NeedsDelimiterNormalization` / `NormalizeDelimitedLine`
    (lines 67-80): `;` → space substitution before whitespace split.
  - Main loop (lines 88-153): comment-prefix handling (`#`),
    optional leading point-count, bad-row soft-skip
    (`if (!px || !py || !pz) continue`).
- Current `Geometry::PointCloudIO::LoadXYZ`
  (`src/geometry/Geometry.PointCloud.IO.cpp` lines 853-944):
  - Honors `#` comments and optional leading point-count.
  - Honors 4-token intensity (line 914-921) and 6+ token RGB at
    offset 3 (line 910-913), but **misreads `.xyzrgb` 7+ token
    layout** because it always reads color from positions 3..5
    instead of the trailing 3.
  - **Rejects** any payload row with `tokens.size() < 3` after the
    first payload line (line 897-900), which incorrectly fails on
    `LH001` scan-line markers and on `;`-only-delimited rows that
    whitespace-split into a single token.
  - **Hard-fails** on any unparseable numeric token (line 904-907)
    rather than soft-skipping bad rows the way the legacy reference
    does.
- Container build environment is missing `clang-20` and `libxrandr`
  dev headers, so `cmake --preset ci` configure may fail at GLFW
  dependency discovery (see prior `GEOIO-002A`-`002G` retirement
  notes and `tasks/backlog/bugs/index.md`). Report build evidence
  honestly; the focused gate may not run in the agent container.

## Required changes
- [x] Edit `src/geometry/Geometry.PointCloud.IO.cpp` only:
  - [x] Inside the existing anonymous namespace, add small XYZ-specific
    helpers placed near the existing `ParseRgb`/`NormalizeColorChannel`
    helpers:
    - [x] `[[nodiscard]] std::optional<glm::vec4> ParseXYZPointColor(
      std::span<const std::string_view> tokens)` mirroring legacy
      `ParsePointColor`: trailing-RGB at `tokens.size() - 3` for
      `tokens.size() >= 7`, RGB at offset 3 for `tokens.size() >= 6`,
      intensity-as-grey at offset 3 for `tokens.size() == 4`.
    - [x] `[[nodiscard]] bool IsXYZScanLineMarker(
      std::span<const std::string_view> tokens)` mirroring legacy
      `IsScanLineMarker`: `tokens.size() == 1`, starts with `"LH"`,
      remainder all decimal digits.
    - [x] `[[nodiscard]] bool XYZNeedsDelimiterNormalization(
      std::string_view line)` returns `line.find(';') !=
      std::string_view::npos`.
    - [x] `void XYZNormalizeDelimitedLine(std::string_view line,
      std::string& scratch)` substitutes every `;` with `' '` into a
      caller-owned scratch string.
  - [x] Refactor `LoadXYZ`:
    - [x] Apply `;`-normalization (via the scratch string) before the
      whitespace split.
    - [x] After splitting, skip empty token vectors and skip scan-line
      markers via `IsXYZScanLineMarker`.
    - [x] Replace the hard-fail on `tokens.size() < 3` with a soft-skip
      (`continue`) and replace the hard-fail on unparseable
      `x`/`y`/`z` numbers with a soft-skip, matching legacy behavior.
    - [x] Replace the inline color-extraction block with
      `ParseXYZPointColor(tokens)`.
    - [x] Preserve the existing `result.Cloud.Reserve(expectedCount)`
      behavior on the optional leading point-count line.
    - [x] Preserve early-termination once `expectedCount > 0` and
      `result.Cloud.VerticesSize() >= expectedCount`.
    - [x] Preserve the empty-cloud → `InvalidPointCloudFormat()` final
      rejection.
- [x] Public module surface (`Geometry.PointCloud.IO.cppm`) does not
  change; the inventory should remain identical apart from the
  regeneration date (matches `GEOIO-002B`/`C`/`D`/`E`/`F`/`G`
  precedent).
- [x] Do not touch any file outside
  `src/geometry/Geometry.PointCloud.IO.cpp`,
  `tests/unit/geometry/Test.GeometryIO.cpp`, and `tasks/`.

## Tests
- [x] Add focused `unit;geometry` coverage to
  `tests/unit/geometry/Test.GeometryIO.cpp`:
  - [x] `LoadsXYZRGBTrailingColor`: file with `x y z` and the last three
    tokens carrying integer RGB; positions and normalized colors
    must match.
  - [x] `LoadsXYZSemicolonDelimited`: rows of the form
    `1.0;2.0;3.0;255;0;0` round-trip into positions and colors.
  - [x] `LoadsXYZSkipsScanLineMarkers`: file contains `LH001` /
    `LH42` rows interleaved with payload rows; the scan-line
    rows must not contribute points and must not abort the load.
  - [x] `LoadsXYZSoftSkipsMalformedRows`: a single non-numeric or
    too-short row in the middle of the file must be skipped,
    not aborted; subsequent payload rows must still load.
  - [x] `LoadXYZRejectsAllMalformedInput`: a file with no parseable
    payload rows (e.g., only `#` comments and `LH###` markers) must
    return `Core::ErrorCode::InvalidFormat` because the cloud is
    empty.
  - [x] Regression: existing `LoadsXYZWithColor` (line 139) must keep
    passing on the canonical 6-token RGB-at-offset-3 layout.

## Docs
- [x] Update `docs/api/generated/module_inventory.md` only if module
  surfaces change in a way the generator picks up
  (`python3 tools/repo/generate_module_inventory.py --root src --out
  docs/api/generated/module_inventory.md`). Adding internal helpers
  and tightening `LoadXYZ` is not expected to change the inventory;
  if the regenerator only changes the date, leave it untouched
  (matches `GEOIO-002B`/`C`/`D`/`E`/`F`/`G` precedent).
- [x] No additional architecture/migration doc edits required for this
  slice; parity-matrix updates remain part of the parent
  `GEOIO-002` task once asset/runtime routing actually drops the
  legacy graphics importers.

## Acceptance criteria
- [x] `Geometry::PointCloudIO::LoadXYZ` returns a populated
  `PointCloudIOResult` for `.xyz`, `.pts`, `.xyzrgb`, and `.txt`
  fixtures covering: canonical 3-token, 4-token intensity, 6-token
  RGB-at-offset-3, 7+-token trailing-RGB, `;`-delimited rows, and
  files that intersperse `LH<digits>` scan-line marker rows.
- [x] A row that is too short or carries non-numeric `x`/`y`/`z`
  tokens is skipped rather than aborting the load.
- [x] A file with no parseable payload rows still returns
  `Core::ErrorCode::InvalidFormat` (existing empty-cloud guard).
- [x] Existing `LoadsXYZWithColor` continues to pass on the canonical
  6-token RGB-at-offset-3 layout.
- [x] `src/geometry/*` imports remain layered (`geometry -> core` only);
  no new asset/runtime/graphics imports introduced.

## Verification
```bash
# Focused gate (full configure may fail in containers missing
# libxrandr / clang-20):
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

# Layering and task-policy gates (do not require build):
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Adding `assets`/`runtime`/`graphics`/`RHI` imports to
  `src/geometry/*`.
- Mixing this importer hardening with mechanical legacy importer
  deletion.
- Adding additional vertex property pass-through (intensity-as-
  attribute, scan-line index, classification, ...) into
  `PointCloud::Cloud` in this slice; only positions and optional
  colors are populated, matching the legacy XYZ loader.
- Touching `src/legacy/Graphics/Importers/*` other than reading them
  as behavioral reference.
- Introducing GPU/Vulkan-only verification requirements.
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

## Completion
- Completed: 2026-05-08.
- Status: done.
- Implementation commit: `55ac47b`
  (`GEOIO-002H: harden geometry-owned XYZ/PTS/XYZRGB text importer`).
- Retired in a follow-up commit on
  `claude/setup-agentic-workflow-xwz1l`.
- Verified in this session:
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (98 task files validated; the same 98 after retirement,
    including this file under `tasks/done/`).
  - `python3 tools/repo/check_layering.py --root src --strict` — no
    layering violations; `geometry` imports remain `geometry -> core`
    only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/repo/generate_module_inventory.py --root src --out
    docs/api/generated/module_inventory.md` — no diff vs. the
    pre-existing inventory; this slice adds anonymous-namespace
    helpers and tightens `LoadXYZ` internally without changing the
    public `Geometry.PointCloud.IO` module surface, matching
    `GEOIO-002B`/`C`/`D`/`E`/`F`/`G` precedent.
- Build/CTest gate not run in this container: `cmake --preset ci`
  configure fails because `clang-20`/`clang++-20` are not installed
  in this agent environment (only `clang-18` is available),
  matching the limitation called out in `Context` and the prior
  `GEOIO-002A`-`002G` retirement notes. The default CPU correctness
  gate (`ctest --test-dir build/ci -R 'GeometryIO' -LE
  'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) should be
  re-run on a host with the documented C++23 toolchain when
  available.
- Notes:
  - Helpers `ParseXYZPointColor`, `IsXYZScanLineMarker`,
    `XYZNeedsDelimiterNormalization`, and
    `XYZNormalizeDelimitedLine` were added to the existing
    anonymous namespace in
    `src/geometry/Geometry.PointCloud.IO.cpp` next to the existing
    `ParseRgb`/`NormalizeColorChannel` helpers.
  - `LoadXYZ` now performs `;` → space normalization into a
    caller-owned scratch string before whitespace splitting, skips
    `LH<digits>` scan-line marker rows before the
    optional-leading-count check, and replaces both the
    too-short-row and unparseable-`x`/`y`/`z`-row hard-fails with
    soft-skip semantics matching the legacy reference. Color
    extraction now goes through `ParseXYZPointColor`, which
    prefers trailing-RGB at `tokens.size() - 3` for
    `tokens.size() >= 7`, falls back to RGB at offset 3 for
    `tokens.size() >= 6`, and finally treats `tokens.size() == 4`
    as intensity-as-grey via `NormalizeColorChannel`.
  - Coverage in `tests/unit/geometry/Test.GeometryIO.cpp` adds
    five cases: `LoadsXYZRGBTrailingColor`,
    `LoadsXYZSemicolonDelimited`, `LoadsXYZSkipsScanLineMarkers`,
    `LoadsXYZSoftSkipsMalformedRows`, and
    `LoadXYZRejectsAllMalformedInput`. The existing
    `LoadsXYZWithColor` (canonical 6-token RGB-at-offset-3 layout)
    remains as a regression case.
  - Remaining `GEOIO-002` scope (granular
    `MeshIOReadStatus`/`PointCloudIOReadStatus` diagnostics enums,
    domain-selection metadata for asset/runtime routing, mesh OFF
    importer parity hardening, OBJ ASCII parity hardening, TGF
    graph importer hardening, and packed-`rgb`/`rgba` PCD plus
    `binary_compressed` LZF decompression) stays tracked under the
    parent backlog task
    `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
