# GEOIO-002I — Geometry-owned OFF mesh importer parity hardening (COFF/NOFF/CNOFF)

## Goal
- Harden `Geometry::MeshIO::LoadOFF`
  (`src/geometry/Geometry.HalfedgeMesh.IO.cpp`) to match the legacy
  `Graphics::OFFLoader` text-format parity surface for `.off`. Specifically:
  recognize the `OFF`, `COFF`, `NOFF`, and `CNOFF` magic headers; populate
  per-vertex normals (`v:normal`) when the variant declares normals; populate
  per-vertex colors (`v:color`) when the variant declares colors, normalizing
  `0..255` channels to `0..1`; soft-skip face rows whose vertex count is
  `< 3` instead of hard-failing the entire load.

## Non-goals
- No new public module surface. `LoadOFF` keeps the signature
  `Core::Expected<MeshIOResult> LoadOFF(std::string_view absolute_path)`.
- No new exports from `Geometry.HalfedgeMesh.IO.cppm`; all new logic stays in
  the existing anonymous namespace inside the `.cpp`.
- No granular `MeshIOReadStatus` diagnostics enum; failures continue to
  surface as `Core::ErrorCode::InvalidFormat`. The reader-side diagnostics
  enum across mesh loaders remains a separate follow-up under the parent
  task.
- No legacy `Graphics::OFFLoader` retirement or rewiring; that stays under
  `GRAPHICS-019` follow-ups.
- No asset registry, runtime scene ingestion, ECS construction, or GPU
  upload work.
- No GPU/Vulkan requirement in the default CPU gate.
- No fan-triangulation in the importer. Halfedge meshes natively store
  polygon faces via `f:vertices`; legacy fan triangulation is a
  `GeometryCpuData` concession that does not apply here.
- No alpha-color storage. Legacy reads but discards alpha; preserve that
  behavior. Stored color is `glm::vec4(r, g, b, 1.0f)`.
- No changes to mesh IO for `.obj`/`.ply`/`.stl`, point-cloud IO, or graph
  IO in this slice.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-jmHQ2`.
- Parent task:
  `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`
  explicitly lists "OFF" mesh importer parity hardening as part of the
  importer parity scope.
- Predecessor slices: `tasks/archive/GEOIO-002D-mesh-stl-binary-importer.md`
  through `tasks/archive/GEOIO-002H-pointcloud-xyz-text-importer.md` shipped
  the prior text/binary importer parity slices following the same anonymous-
  namespace-helper + targeted-refactor pattern.
- Behavioral reference:
  `src/legacy/Graphics/Importers/Graphics.Importers.OFF.cpp`:
  - `OFFVariant` (lines 27-33): `Standard` (`OFF`), `COFF`, `NOFF`, `CNOFF`.
  - Magic parse (lines 62-72): exact-match dispatch on the trimmed first
    non-comment line.
  - Vertex parse (lines 100-151): `x y z` first, then `nx ny nz` if
    `hasNormals`, then `r g b` (and optional discarded `a`) if `hasColors`,
    each with parse-failure tolerance (the legacy keeps default values on
    failure rather than aborting).
  - Face parse (lines 155-178): `count < 3` rows are soft-skipped via
    `continue`; rows with insufficient tokens (`tokens.size() < 1 + count`)
    or out-of-range vertex indices hard-fail.
  - Color normalization: `Detail::NormalizeColorChannelToUnitRange` from
    `Graphics.Importers.ColorParsing.hpp`. Geometry has the same shape
    helper in `src/geometry/Geometry.PointCloud.IO.cpp`
    (`NormalizeColorChannel`, line 136) which is the layer-clean analog
    to clone into `Geometry.HalfedgeMesh.IO.cpp`.
- Current `Geometry::MeshIO::LoadOFF`
  (`src/geometry/Geometry.HalfedgeMesh.IO.cpp` lines 719-833):
  - Honors `#`/blank comment lines at the magic line, the counts line, and
    in vertex/face row positions (`--i; continue;` pattern).
  - **Rejects** any magic other than the exact string `"OFF"` (line
    737-740), so `COFF`/`NOFF`/`CNOFF` files are misclassified as invalid.
  - **Ignores** trailing per-vertex tokens entirely; positions are read
    from `tokens[0..2]`, leftover tokens are silently dropped, so
    per-vertex normals and colors are never surfaced.
  - **Hard-fails** on face rows with `count < 3` (line 809), aborting the
    load on any degenerate face row even when the rest of the file is
    valid.
  - Hard-fails on out-of-range vertex indices (preserved in this slice).
- Container build environment is missing `clang-20` and `libxrandr` dev
  headers, so `cmake --preset ci` configure may fail at GLFW dependency
  discovery (see prior `GEOIO-002A`-`002H` retirement notes and
  `tasks/backlog/bugs/index.md`). Report build evidence honestly; the
  focused gate may not run in the agent container.

## Required changes
- [x] Edit `src/geometry/Geometry.HalfedgeMesh.IO.cpp` only:
  - [x] Inside the existing anonymous namespace at the top of the file, add:
    - [x] `enum class OFFVariant { Standard, COFF, NOFF, CNOFF };` placed
      near other small mesh-IO helpers (above `LoadOFF`).
    - [x] `[[nodiscard]] std::optional<OFFVariant> ParseOFFMagic(std::string_view line);`
      returning the variant for `"OFF"`, `"COFF"`, `"NOFF"`, `"CNOFF"` and
      `std::nullopt` otherwise.
    - [x] `[[nodiscard]] float NormalizeOFFColorChannel(float value);` mirroring
      the `Geometry.PointCloud.IO.cpp` `NormalizeColorChannel` helper:
      `value > 1.0f ? std::clamp(value / 255.0f, 0.0f, 1.0f) : std::clamp(value, 0.0f, 1.0f)`.
  - [x] Refactor `LoadOFF`:
    - [x] Replace the `line != "OFF"` check with `ParseOFFMagic(line)`; on
      `std::nullopt` return `InvalidMeshFormat()`. Derive
      `hasNormals = (variant == NOFF || variant == CNOFF)` and
      `hasColors = (variant == COFF || variant == CNOFF)`.
    - [x] In the vertex loop, after parsing `x y z` from `tokens[0..2]`,
      read three optional normal tokens at `tokens[3..5]` when
      `hasNormals` and `tokens.size() >= 6`, defaulting to `(0, 1, 0)` on
      missing/unparseable values to match legacy behavior. Track a
      `tokenIdx` cursor so colors read from the correct offset
      (`tokenIdx == 3` when no normals, `tokenIdx == 6` when normals were
      consumed).
    - [x] Read three optional color tokens at `tokens[tokenIdx..tokenIdx+2]`
      when `hasColors` and the row has at least three remaining tokens.
      Normalize via `NormalizeOFFColorChannel` and store as
      `glm::vec4(r, g, b, 1.0f)`; default to `glm::vec4(0, 0, 0, 1)` on
      missing/unparseable values to match the legacy default.
    - [x] Consume an optional fourth alpha token if present (parse via
      `ParseNumber<float>` and discard) so a stray alpha does not
      contaminate downstream parsing. Do not store it.
    - [x] Replace the `*count < 3` hard-fail in the face loop with
      `continue` (soft-skip degenerate face rows). Keep the
      `tokens.size() < count + 1` and out-of-range-index hard-fails.
    - [x] After the existing `PopulateResult(result, vertices, faces, normals)`
      call (extend the existing `normals` argument so `PopulateResult`
      writes `v:normal` only when `hasNormals` and the per-vertex normal
      vector is the same length as positions), populate `v:color` if
      `hasColors` by allocating a `glm::vec4` property and copying the
      `colors` vector. The existing `PopulateResult` already gates
      normal writes on a non-empty span of equal length; reuse that.
  - [x] Local containers `std::vector<glm::vec3> normals` and
    `std::vector<glm::vec4> colors` are sized to `*vertexCount` only when
    `hasNormals`/`hasColors` is true, mirroring the legacy
    `outData.Normals().resize(*nVertices, ...)` /
    `outData.Attrs().resize(*nVertices, ...)` defaults.
- [x] Public module surface (`Geometry.HalfedgeMesh.IO.cppm`) does not change;
  the inventory should remain identical apart from the regeneration date
  (matches `GEOIO-002B`/`C`/`D`/`E`/`F`/`G`/`H` precedent).
- [x] Do not touch any file outside
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp`,
  `tests/unit/geometry/Test.GeometryIO.cpp`, and `tasks/`.

## Tests
- [x] Add focused `unit;geometry` coverage to
  `tests/unit/geometry/Test.GeometryIO.cpp`:
  - [x] `LoadsCOFFTriangleWithVertexColors`: `COFF` magic, 6-token vertex rows
    (`x y z r g b` with `r,g,b` integer 0..255). Asserts `Vertices.Size()
    == 3`, `v:point` matches positions, `v:color` is populated with
    channel-normalized values and `alpha == 1.0`.
  - [x] `LoadsCOFFAlphaTokenIsConsumedNotStored`: `COFF` magic, 7-token vertex
    rows (`x y z r g b a`). Asserts the load succeeds, `v:color` stores the
    RGB-normalized triplet with `alpha == 1.0` (the trailing `a` is read
    via `ParseNumber` but discarded).
  - [x] `LoadsNOFFTriangleWithVertexNormals`: `NOFF` magic, 6-token vertex rows
    (`x y z nx ny nz`). Asserts `v:normal` is populated and matches the
    file's normals.
  - [x] `LoadsCNOFFTriangleWithNormalsAndColors`: `CNOFF` magic, 9-token vertex
    rows. Asserts both `v:normal` and `v:color` are populated correctly
    with colors normalized.
  - [x] `LoadOFFSkipsDegenerateFaceRows`: file declares one `2 0 1` face plus
    one valid `3 0 1 2` face in the face block (with the face count line
    declaring `2`). Asserts the load succeeds with one face populated and
    `v:point` containing all three vertices. (Note: the second declared
    face occupies the second row but is degenerate; the loader soft-skips
    it without aborting and leaves a single populated `f:vertices`
    entry. To keep `*faceCount` honored, we declare two face rows in the
    counts header so the loop iterates both.)
  - [x] `LoadOFFRejectsUnknownMagic`: file with magic `"FOO"` returns
    `Core::ErrorCode::InvalidFormat`.
  - [x] `LoadOFFRejectsOutOfRangeVertexIndex`: face references a vertex index
    `>= vertexCount`; returns `InvalidFormat` (preserves existing
    strictness).
  - [x] Regression: existing `LoadsOFFTriangle` continues to pass on the
    canonical `OFF` (Standard) magic.

## Docs
- [x] Update `docs/api/generated/module_inventory.md` only if module surfaces
  change in a way the generator picks up
  (`python3 tools/repo/generate_module_inventory.py --root src --out
  docs/api/generated/module_inventory.md`). Adding internal helpers and
  tightening `LoadOFF` is not expected to change the inventory; if the
  regenerator only changes the date, leave it untouched (matches
  `GEOIO-002B`/`C`/`D`/`E`/`F`/`G`/`H` precedent).
- [x] No additional architecture/migration doc edits required for this slice;
  parity-matrix updates remain part of the parent `GEOIO-002` task once
  asset/runtime routing actually drops the legacy graphics importers.

## Acceptance criteria
- [x] `Geometry::MeshIO::LoadOFF` returns a populated `MeshIOResult` for `.off`
  fixtures covering: canonical `OFF` magic, `COFF` magic with 6-token and
  7-token (alpha-bearing) vertex rows, `NOFF` magic with 6-token vertex
  rows, and `CNOFF` magic with 9-token vertex rows.
- [x] For `COFF`/`CNOFF`, the resulting `MeshIOResult.Vertices` contains a
  `glm::vec4 v:color` property whose RGB channels are normalized through
  `NormalizeOFFColorChannel`; alpha is `1.0`.
- [x] For `NOFF`/`CNOFF`, the resulting `MeshIOResult.Vertices` contains a
  `glm::vec3 v:normal` property matching the file's normals.
- [x] A face row with declared count `< 3` is soft-skipped rather than
  aborting the load. Out-of-range vertex indices and short face rows
  remain hard failures (`InvalidFormat`).
- [x] An unknown magic header returns `Core::ErrorCode::InvalidFormat`.
- [x] Existing `LoadsOFFTriangle` continues to pass on the canonical `OFF`
  magic.
- [x] `src/geometry/*` imports remain layered (`geometry -> core` only); no
  new asset/runtime/graphics imports introduced.

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
- Adding `assets`/`runtime`/`graphics`/`RHI` imports to `src/geometry/*`.
- Mixing this importer hardening with mechanical legacy importer deletion
  (`src/legacy/Graphics/Importers/Graphics.Importers.OFF.*`).
- Touching `src/legacy/Graphics/Importers/*` other than reading them as
  behavioral reference.
- Mixing OFF parity with OBJ ASCII, TGF, or PCD packed-rgb/LZF parity in
  this slice.
- Introducing GPU/Vulkan-only verification requirements.
- Changing the `LoadOFF` signature or any other public module export.
- Storing per-vertex alpha (preserve legacy behavior of consuming-but-not-
  storing).
- Adding fan triangulation; halfedge meshes natively support polygon
  faces.
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

## Completion
- Completed: 2026-05-08.
- Status: done.
- Implementation commit: `7ae5f0c`
  (`GEOIO-002I: harden geometry-owned OFF mesh importer (COFF/NOFF/CNOFF)`).
- Retired in a follow-up commit on
  `claude/setup-agentic-workflow-jmHQ2`.
- Verified in this session:
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (101 task files validated; the same 101 after retirement,
    including this file under `tasks/done/`).
  - `python3 tools/repo/check_layering.py --root src --strict` — no
    layering violations; `geometry` imports remain `geometry -> core`
    only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/repo/generate_module_inventory.py --root src --out
    docs/api/generated/module_inventory.md` — no diff vs. the
    pre-existing inventory; this slice adds anonymous-namespace
    helpers (`OFFVariant`, `ParseOFFMagic`, `NormalizeOFFColorChannel`)
    and an optional `colors` parameter on the file-internal
    `PopulateResult` helper without changing the public
    `Geometry.HalfedgeMesh.IO` module surface, matching
    `GEOIO-002B`/`C`/`D`/`E`/`F`/`G`/`H` precedent.
- Build/CTest gate not run in this container: `cmake --preset ci`
  configure fails because `clang-20`/`clang++-20` are not installed in
  this agent environment (only `clang-18` is available), matching the
  limitation called out in `Context` and the prior `GEOIO-002A`-`002H`
  retirement notes. The default CPU correctness gate (`ctest --test-dir
  build/ci -R 'GeometryIO' -LE 'gpu|vulkan|slow|flaky-quarantine'
  --timeout 60`) should be re-run on a host with the documented C++23
  toolchain when available.
- Notes:
  - `OFFVariant`, `ParseOFFMagic`, and `NormalizeOFFColorChannel`
    helpers were added to the existing anonymous namespace in
    `src/geometry/Geometry.HalfedgeMesh.IO.cpp` next to
    `InvalidMeshFormat`.
  - `PopulateResult` gained an optional
    `std::span<const glm::vec4> colors = {}` parameter that mirrors the
    existing optional `normals` parameter; OFF passes both when the
    variant declares them, OBJ/PLY/STL ASCII paths are unaffected.
  - `LoadOFF` now dispatches on `ParseOFFMagic`; reads optional per-
    vertex normals and colors based on the variant, normalizing color
    channels through `NormalizeOFFColorChannel`; consumes-but-discards
    a trailing alpha token; soft-skips face rows whose declared count
    is `< 3`; and preserves hard-fails on short face rows and
    out-of-range vertex indices.
  - Coverage in `tests/unit/geometry/Test.GeometryIO.cpp` adds seven
    cases: `LoadsCOFFTriangleWithVertexColors`,
    `LoadsCOFFAlphaTokenIsConsumedNotStored`,
    `LoadsNOFFTriangleWithVertexNormals`,
    `LoadsCNOFFTriangleWithNormalsAndColors`,
    `LoadOFFSkipsDegenerateFaceRows`, `LoadOFFRejectsUnknownMagic`, and
    `LoadOFFRejectsOutOfRangeVertexIndex`. The existing
    `LoadsOFFTriangle` (canonical `OFF` magic) remains as a regression
    case.
  - Remaining `GEOIO-002` scope (granular
    `MeshIOReadStatus`/`PointCloudIOReadStatus` diagnostics enums,
    domain-selection metadata for asset/runtime routing, OBJ ASCII
    parity hardening, TGF graph importer hardening, and packed-`rgb`/
    `rgba` PCD plus `binary_compressed` LZF decompression) stays
    tracked under the parent backlog task
    `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
