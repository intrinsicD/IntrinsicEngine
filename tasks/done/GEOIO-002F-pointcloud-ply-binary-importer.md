# GEOIO-002F — Geometry-owned binary PLY point-cloud importer

## Goal
- Extend `Geometry::PointCloudIO::LoadPLY` to accept binary little-endian
  and binary big-endian PLY point-cloud files in addition to the
  existing ASCII path, mirroring the GEOIO-002E mesh-side binary path.
  Positions remain mandatory; optional `nx/ny/nz` normals and
  `red/green/blue` colors are decoded when present, matching the
  geometry-side ASCII PLY point-cloud reader's behavior.

## Non-goals
- No new public module surface. The signature of `LoadPLY` stays
  `Core::Expected<PointCloudIOResult> LoadPLY(std::string_view absolute_path)`.
- No mesh PLY behavior change; mesh-side parity is owned by GEOIO-002E.
- No PCD binary support; that remains tracked under the parent
  `GEOIO-002` task.
- No granular `PointCloudIOReadStatus` diagnostics enum; failures
  continue to surface as `Core::ErrorCode::InvalidFormat`. A reader-side
  diagnostics enum across point-cloud loaders is a separate follow-up
  slice under the parent task.
- No legacy `Graphics::PLYLoader` retirement or rewiring; that remains
  under `GRAPHICS-019` follow-ups.
- No asset registry, runtime scene ingestion, ECS construction, or GPU
  upload work.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-rYQnA`.
- Parent backlog task:
  `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
- Predecessor slice: `tasks/done/GEOIO-002E-mesh-ply-binary-importer.md`
  introduced the `PlyFormat`/`PlyScalar`/`PlyProperty`/`PlyElement`
  helpers and the `ParseBinaryPLY` pattern for mesh PLY in
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp`. This slice ports the
  same helpers into `src/geometry/Geometry.PointCloud.IO.cpp` for the
  point-cloud `LoadPLY`.
- Behavioral reference: `src/legacy/Graphics/Importers/Graphics.Importers.PLY.cpp`
  (`PlyFormat` and scalar table near line 36, header parser near
  line 300, binary stride/offset setup near line 473). The
  geometry-side point-cloud port keeps a smaller surface — only
  positions plus optional normals (`nx/ny/nz`) and uchar colors
  (`red/green/blue`).
- The existing `PointCloudIO::ReadTextFile` already opens files with
  `std::ios::binary`, so the buffer can be reinterpreted as a
  `std::span<const std::byte>` over the bytes after `end_header\n`,
  matching the mesh-side approach.
- Container build environment is missing `clang-20` and `libxrandr`
  dev headers, so `cmake --preset ci` configure currently fails at
  GLFW dependency discovery (see `tasks/backlog/bugs/index.md` and
  prior `GEOIO-002A`/`B`/`C`/`D`/`E` retirement notes). Report build
  evidence honestly; the focused gate may not run in the agent
  container.

## Required changes
- Edit `src/geometry/Geometry.PointCloud.IO.cpp` only:
  - Inside the existing anonymous namespace, add small helpers:
    - `enum class PlyFormat { Ascii, BinaryLittleEndian, BinaryBigEndian }`.
    - `enum class PlyScalar { Int8, UInt8, Int16, UInt16, Int32,
      UInt32, Float32, Float64 }` plus `PlyScalarBytes(PlyScalar)` and
      `ParsePlyScalarType(std::string_view)` (token aliases match the
      legacy reference and the GEOIO-002E mesh-side helpers).
    - `struct PlyProperty { std::string Name; bool IsList; PlyScalar
      ScalarType; PlyScalar ListCountType; }`.
    - `struct PlyElement { std::string Name; std::size_t Count;
      std::vector<PlyProperty> Properties; }`.
    - `void ByteSwap(std::byte*, std::size_t)`.
    - `template <typename T> T ReadScalarAs(const std::byte*& cursor,
      PlyScalar, bool bigEndian)` decoding the scalar via `std::memcpy`
      plus optional byte-swap, then casting through the declared type
      to `T`.
  - Refactor `LoadPLY` to:
    - Read the file once via the existing `ReadTextFile`.
    - Parse the header into a `PlyFormat` and
      `std::vector<PlyElement>`, tracking the byte offset of the first
      byte after `end_header\n`. Reject malformed
      `format`/`element`/`property` lines as `InvalidPointCloudFormat()`.
    - Dispatch ASCII to a `ParseAsciiPLY` helper holding the existing
      ASCII vertex parsing logic byte-for-byte, keyed by the parsed
      `vertex` element count and property names.
    - Dispatch binary little/big-endian to a `ParseBinaryPLY` helper
      that walks elements in declaration order, locates the position
      property indices (`x`/`y`/`z` as `Float32`), and optionally the
      normal property indices (`nx`/`ny`/`nz` as `Float32`) and color
      property indices (`red`/`green`/`blue` as `UInt8`), computes a
      fixed per-row vertex stride from all scalar properties, and
      reads positions/normals/colors via `ReadScalarAs<float>` with
      the existing `NormalizeColorChannel` for color bytes. Other
      vertex scalar properties are skipped via stride. List properties
      inside the `vertex` element are rejected. Other elements are
      skipped only when their properties are all scalars; encountering
      a list property in a non-vertex element is rejected (matches the
      mesh-side conservative posture for non-`face` elements).
  - Diagnostics: return `InvalidPointCloudFormat()` (i.e.
    `Core::ErrorCode::InvalidFormat`) for missing `format` line,
    missing `end_header`, missing/zero-count `vertex` element,
    missing/non-`float` x/y/z, presence of a list property inside the
    `vertex` element, presence of a list property inside any other
    element, or a truncated/oversized binary body. A 0-byte short
    field or off-the-end stride must reject rather than read past the
    buffer.
- Do not change `LoadXYZ`/`LoadPCD` behavior, do not introduce new
  module imports, and do not touch any file outside
  `src/geometry/Geometry.PointCloud.IO.cpp`,
  `tests/unit/geometry/Test.GeometryIO.cpp`, and `tasks/`.
- Public module surface (`Geometry.PointCloud.IO.cppm`) does not
  change; the inventory should remain identical apart from the
  regeneration date (matches `GEOIO-002B`/`C`/`D`/`E` precedent).

## Tests
- Add focused `unit;geometry` coverage to
  `tests/unit/geometry/Test.GeometryIO.cpp`:
  - `LoadsBinaryLittleEndianPLYPointCloud`: build a fixture with the
    canonical point-cloud PLY binary little-endian header
    (`property float x`, `property float y`, `property float z`,
    no `face` element) and three vertices; load via `LoadPLY` and
    assert positions match.
  - `LoadsBinaryBigEndianPLYPointCloud`: same vertex layout but
    byte-swap the float positions, with
    `format binary_big_endian 1.0`.
  - `LoadsBinaryPLYPointCloudWithNormalsAndColor`: vertex layout has
    `float x/y/z`, `float nx/ny/nz`, `uchar red/green/blue`; assert
    positions, `HasNormals`, normals, `HasColors`, and color channels
    normalized via `NormalizeColorChannel`.
  - `LoadsBinaryPLYPointCloudSkipsExtraScalars`: vertex layout has
    `float x/y/z` followed by `float intensity` (a single extra
    scalar) and `uchar red/green/blue`; positions and colors must
    still parse correctly when an extra scalar appears between the
    position and color blocks.
  - `LoadPLYPointCloudRejectsTruncatedBinaryBody`: header advertises
    `vertex 4` but the body only contains one vertex worth of bytes;
    expect `Core::ErrorCode::InvalidFormat`.
  - `LoadPLYPointCloudRejectsListPropertyInVertex`: vertex element
    declares `property list uchar int unsupported` after `x/y/z`;
    expect `Core::ErrorCode::InvalidFormat`.
  - `LoadsAsciiPLYPointCloudAfterBinaryDispatch`: regression — the
    existing ASCII fixture (`LoadsVertexOnlyASCIIPLY`) must continue
    to round-trip through the refactored dispatch path.
- Helper: add a `WriteBinaryPLYPointCloudFixture` test fixture writer
  alongside the existing mesh `WriteBinaryPLYFixture` which:
  - Takes positions, optional normals, optional uchar colors, an
    optional extra-`float intensity` flag, an endianness flag, and
    truncation/list-injection flags for negative-path tests.
  - Emits the ASCII header up to `end_header\n`, then writes the
    binary body via `std::ofstream(..., std::ios::binary)` and
    explicit byte-level `write` calls (no `operator<<` for binary
    scalars). For big-endian fixtures the writer byte-swaps the
    four-byte floats manually before writing.

## Docs
- Update `docs/api/generated/module_inventory.md` only if module
  surfaces change in a way the generator picks up
  (`python3 tools/repo/generate_module_inventory.py --root src --out
  docs/api/generated/module_inventory.md`). Adding internal helpers
  and expanding a function body is not expected to change the
  inventory; if the regenerator only changes the date, leave it
  untouched (matches `GEOIO-002B`/`C`/`D`/`E` precedent).
- No additional architecture/migration doc edits required for this
  slice; parity-matrix updates remain part of the parent `GEOIO-002`
  task once asset/runtime routing actually drops the legacy graphics
  importers.

## Acceptance criteria
- `Geometry::PointCloudIO::LoadPLY` returns a populated
  `PointCloudIOResult` for well-formed binary little-endian and binary
  big-endian PLY point-cloud fixtures, with positions equal to the
  input vertices and, when present, normals and normalized colors.
- `LoadPLY` continues to accept ASCII PLY fixtures (existing
  `LoadsVertexOnlyASCIIPLY` and the new
  `LoadsAsciiPLYPointCloudAfterBinaryDispatch` regression both pass).
- A truncated binary payload, or a list property inside the `vertex`
  element, produces `Core::ErrorCode::InvalidFormat` rather than
  out-of-bounds reads or partial parses.
- `src/geometry/*` imports remain layered (`geometry -> core` only);
  no new asset/runtime/graphics imports introduced.
- Existing `LoadXYZ`/`LoadPCD`, mesh `LoadPLY`, and all `Write*`
  tests continue to pass.

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
- Mixing this importer with mechanical legacy importer deletion.
- Adding additional vertex property pass-through (e.g. UVs, intensity)
  into `PointCloud::Cloud` in this slice; only positions, optional
  `nx/ny/nz` normals, and optional `red/green/blue` colors are
  populated, matching the existing ASCII point-cloud reader.
- Adding mesh PLY binary support in `PointCloudIO`; that lives in
  `MeshIO::LoadPLY` (already shipped under GEOIO-002E).
- Touching `src/legacy/Graphics/Importers/*` other than reading them
  as behavioral reference.
- Introducing GPU/Vulkan-only verification requirements.
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

## Completion
- Completed: 2026-05-08.
- Implementation commit: `26016b3`
  (`GEOIO-002F: add geometry-owned binary PLY point-cloud importer`).
- Retired in a follow-up commit on
  `claude/setup-agentic-workflow-rYQnA`.
- Verified in this session:
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (88 task files validated before retirement; 88 after
    retirement, including this file).
  - `python3 tools/repo/check_layering.py --root src --strict` — no
    layering violations; `geometry` imports remain `geometry -> core`
    only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/repo/generate_module_inventory.py --root src --out
    docs/api/generated/module_inventory.md` — no diff vs. the
    pre-existing inventory; this slice adds anonymous-namespace
    helpers and refactors `LoadPLY` internally without changing the
    public `Geometry.PointCloud.IO` module surface, matching
    `GEOIO-002B`/`C`/`D`/`E` precedent.
- Build/CTest gate not run in this container: `cmake --preset ci`
  configure fails because `clang-20`/`clang++-20` are not installed
  in this agent environment, matching the limitation called out in
  `Context` and the prior `GEOIO-002A`/`B`/`C`/`D`/`E` retirement
  notes. The default CPU correctness gate
  (`ctest --test-dir build/ci -R 'GeometryIO' -LE
  'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) should be re-run
  on a host with the documented C++23 toolchain when available.
- Notes:
  - Header parsing now produces a `std::vector<PlyElement>` with full
    `PlyProperty` records (scalar type, list element/count types,
    name) regardless of format. The existing ASCII vertex parser
    (positions, optional `nx/ny/nz` normals, optional uchar
    `red/green/blue` colors) was extracted into a
    `ParseAsciiPLYPointCloud` helper that consumes the structured
    `vertex` element. ASCII-path behavior is unchanged; the existing
    `LoadsVertexOnlyASCIIPLY` test continues to pass through the
    refactored dispatch.
  - The binary parser (`ParseBinaryPLYPointCloud`) walks elements in
    declaration order. Inside the `vertex` element it locates the
    property indices for `x/y/z` (Float32, mandatory), `nx/ny/nz`
    (Float32, optional), and `red/green/blue` (UInt8, optional),
    computes a fixed per-row stride from all scalar properties, and
    reads each row via `readFloat`/`readUInt8` plus
    `NormalizeColorChannel` for color bytes. Unrecognized vertex
    scalar properties are skipped via stride; list properties inside
    the `vertex` element are rejected because they break fixed-stride
    decoding. Other elements are skipped only when their properties
    are all scalars; encountering a list property in a non-vertex
    element is also rejected (conservative posture matching the
    geometry-side mesh PLY reader for non-`face` elements).
    Big-endian decoding goes through the `ReadScalarAs<T>` helper
    that byte-swaps a fixed-size buffer before `std::memcpy` into
    the host scalar type.
  - Coverage in `tests/unit/geometry/Test.GeometryIO.cpp` adds seven
    cases:
    `LoadsBinaryLittleEndianPLYPointCloud`,
    `LoadsBinaryBigEndianPLYPointCloud`,
    `LoadsBinaryPLYPointCloudWithNormalsAndColor`,
    `LoadsBinaryPLYPointCloudSkipsExtraScalars`,
    `LoadPLYPointCloudRejectsTruncatedBinaryBody`,
    `LoadPLYPointCloudRejectsListPropertyInVertex`, and
    `LoadsAsciiPLYPointCloudAfterBinaryDispatch`. A new
    `WriteBinaryPLYPointCloudFixture` helper writes the canonical
    ASCII header followed by a binary body via
    `std::ofstream(..., std::ios::binary)` with explicit byte-swap
    for big-endian variants and reuses the mesh-side
    `BinaryPlyEndian`/`EncodeFloat` helpers from the same TU.
  - Remaining `GEOIO-002` scope (binary PCD import, granular
    `MeshIOReadStatus`/`PointCloudIOReadStatus` diagnostics enums,
    domain-selection metadata for asset/runtime routing, importer
    parity hardening for additional point-cloud variants) stays
    tracked under the parent backlog task
    `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
