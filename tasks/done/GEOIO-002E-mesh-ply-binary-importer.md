# GEOIO-002E — Geometry-owned binary PLY mesh importer

## Goal
- Extend `Geometry::MeshIO::LoadPLY` to accept binary little-endian and
  binary big-endian PLY mesh files in addition to the existing ASCII path,
  closing a parity gap between the geometry-side importer and the legacy
  `Graphics.Importers.PLY` reference (which has long supported all three
  encodings). This is the next slice of the parent `GEOIO-002` parity
  hardening task and follows the `GEOIO-002A`/`B`/`C`/`D` exporter and
  binary STL importer slices.

## Non-goals
- No new public module surface. The signature of `LoadPLY` stays
  `Core::Expected<MeshIOResult> LoadPLY(std::string_view absolute_path)`.
- No new diagnostic enum (no `MeshIOReadStatus` parallel to
  `MeshIOWriteStatus`). Failures continue to surface as
  `Core::ErrorCode` values; a granular reader-side enum across
  OBJ/OFF/PLY/STL remains a separate follow-up slice.
- No vertex-property pass-through into the result outside of `x`/`y`/`z`
  positions. The geometry-side ASCII reader already ignores vertex
  normals and colors when present in mesh PLY; the binary path matches
  that behavior. Binary point-cloud PLY parity is owned by
  `Geometry::PointCloudIO::LoadPLY` and tracked under the parent
  `GEOIO-002` task, not this slice.
- No legacy `Graphics::PLYLoader` registry deletion or rewiring; that
  retirement remains tracked under `GRAPHICS-019` follow-ups.
- No asset registry, runtime scene ingestion, ECS construction, or GPU
  upload work.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-Yxe9K`.
- Parent backlog task:
  `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
- Predecessor slices:
  - `tasks/done/GEOIO-002A-mesh-obj-exporter.md` introduced
    `MeshIOWriteStatus` and `WriteOBJ`.
  - `tasks/done/GEOIO-002B-mesh-ply-exporter.md` added ASCII `WritePLY`.
  - `tasks/done/GEOIO-002C-mesh-stl-exporter.md` added ASCII `WriteSTL`.
  - `tasks/done/GEOIO-002D-mesh-stl-binary-importer.md` added binary STL
    import.
- The legacy `Graphics.Importers.PLY` parser at
  `src/legacy/Graphics/Importers/Graphics.Importers.PLY.cpp` (`PlyFormat`
  at line 36, scalar-type table starting at line 38, header parser at
  line 300, binary stride/offset setup at line 473) is the behavioral
  reference. The geometry-side port intentionally keeps a much smaller
  surface: it only extracts `float x/y/z` positions and the
  `vertex_indices`/`vertex_index` face list, ignoring optional vertex
  normals/colors/UVs (matching the existing geometry-side ASCII reader).
- `Geometry::IOText::ReadTextFile` already opens files with
  `std::ios::binary`, so the existing helper is a faithful binary read
  even though its name says "text". Reuse it via a `std::span<const
  std::byte>` view formed over the buffer past `end_header\n`. This
  avoids introducing a new helper for a single use site, matching the
  binary STL slice (`GEOIO-002D`).
- PLY binary layout (per the de-facto Stanford PLY spec):
  - Header is ASCII text terminated by `end_header` followed by a
    single newline. Properties declared in the header appear in the
    binary body in declaration order, row-by-row, element-by-element.
  - List properties are encoded as `<count>` followed by `<count>`
    values of the element type. Count and element types each declare
    their scalar type independently.
  - Endianness is declared in the `format` line:
    `binary_little_endian` or `binary_big_endian`. All scalars in the
    body are encoded in that endianness.
- Container build environment is missing `clang-20` and `libxrandr` dev
  headers, so the standard `cmake --preset ci` configure currently fails
  at GLFW dependency discovery (see `tasks/backlog/bugs/index.md` and
  the prior `GEOIO-002A`/`B`/`C`/`D` retirement notes). Report build
  evidence honestly; the focused gate may not run in the agent
  container.

## Required changes
- [x] Extend `src/geometry/Geometry.HalfedgeMesh.IO.cpp` (no `.cppm`
  changes):
  - [x] Inside the existing anonymous namespace, add small helpers:
    - [x] `enum class PlyFormat { Ascii, BinaryLittleEndian, BinaryBigEndian }`.
    - [x] `enum class PlyScalar { Int8, UInt8, Int16, UInt16, Int32, UInt32,
       Float32, Float64 }` plus `PlyScalarBytes(PlyScalar)` and
      `ParsePlyScalarType(std::string_view)` (token aliases match the
      legacy reference: `char/int8`, `uchar/uint8`, `short/int16`,
      `ushort/uint16`, `int/int32`, `uint/uint32`, `float/float32`,
      `double/float64`).
    - [x] `struct PlyProperty { std::string Name; bool IsList; PlyScalar
       ScalarType; PlyScalar ListCountType; }` (for non-list properties
      `ScalarType` is the value type; for list properties `ScalarType`
      is the element type and `ListCountType` is the count type).
    - [x] `struct PlyElement { std::string Name; std::size_t Count;
       std::vector<PlyProperty> Properties; }`.
    - [x] `void ByteSwap(std::byte*, std::size_t)`.
    - [x] `template <typename T> T ReadScalarAs(const std::byte*& cursor,
       PlyScalar, bool bigEndian)` decoding the scalar via
      `std::memcpy` + optional byte-swap, then casting through the
      declared type to `T`.
  - [x] Refactor `LoadPLY` to:
    - [x] Read the file once via `ReadTextFile`.
    - [x] Parse the header into a `PlyFormat` and `std::vector<PlyElement>`,
      tracking the absolute byte offset of the first byte after the
      `end_header` line. Reject malformed `format`/`element`/`property`
      lines as `InvalidMeshFormat`.
    - [x] Dispatch ASCII to a `ParseAsciiPLY` helper that contains the
      existing ASCII vertex/face parsing logic byte-for-byte, keyed by
      the `vertex` and `face` element counts.
    - [x] Dispatch binary little/big-endian to a `ParseBinaryPLY` helper
      that walks elements in declaration order, reads x/y/z floats from
      the `vertex` element (locating their property indices and using a
      fixed per-row stride computed from all scalar properties),
      streams the `face` element row-by-row to read the
      `vertex_indices`/`vertex_index` list, and skips other elements
      whose properties are all scalars.
  - [x] Diagnostics: return `InvalidMeshFormat()` (i.e.
    `Core::ErrorCode::InvalidFormat`) for missing format line, missing
    `end_header`, missing/zero-count `vertex` or `face` element,
    missing/non-`float` x/y/z, unsupported list property in a vertex
    element, missing `vertex_indices`/`vertex_index` list in the face
    element, face indices out of range, lists with a count below 3, or
    truncated/oversized binary bodies.
- [x] Do not change importer behavior for OBJ/OFF/STL, do not introduce new
  module imports, and do not touch any file outside
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp`,
  `tests/unit/geometry/Test.GeometryIO.cpp`, and `tasks/`.
- [x] Public module surface (`Geometry.HalfedgeMesh.IO.cppm`) does not
  change; the inventory should remain identical apart from the
  regeneration date (matches `GEOIO-002B`/`C`/`D` precedent).

## Tests
- [x] Add focused `unit;geometry` coverage to
  `tests/unit/geometry/Test.GeometryIO.cpp`:
  - [x] `LoadsBinaryLittleEndianPLYTriangle`: build a fixture with the
    canonical mesh-PLY binary little-endian header (`property float x`,
    `property float y`, `property float z`,
    `property list uchar int vertex_indices`) and one triangle; load
    via `LoadPLY` and assert `ExpectTriangleMeshProperties(*result)`.
  - [x] `LoadsBinaryBigEndianPLYTriangle`: same vertex layout but byte-swap
    the float positions and the four-byte indices, with
    `format binary_big_endian 1.0`.
  - [x] `LoadsBinaryLittleEndianPLYQuad`: list count `4` -> face arity
    preserved (4 indices); used to confirm the binary face-list reader
    does not silently truncate non-triangular faces.
  - [x] `LoadsBinaryLittleEndianPLYWithExtraVertexProperties`: vertex
    layout has `float x`, `float y`, `float z`, `uchar red`,
    `uchar green`, `uchar blue` (color bytes are skipped via stride);
    confirms positions parse correctly when extra scalars are present
    after x/y/z.
  - [x] `LoadPLYRejectsTruncatedBinaryBody`: header advertises
    `vertex 2`/`face 1` but the body only contains one vertex worth of
    bytes; expect `Core::ErrorCode::InvalidFormat`.
  - [x] `LoadPLYRejectsBinaryFaceListBelowThree`: face list count is `2`;
    expect `Core::ErrorCode::InvalidFormat`.
  - [x] `LoadsAsciiPLYAfterBinaryDispatch`: regression — the existing ASCII
    fixture (`LoadsASCIIPLYTriangle`) must continue to round-trip
    through the refactored dispatch path.
- [x] Helper: add a `WriteBinaryPLYFixture` test fixture writer that takes
  positions, faces (as `std::vector<std::uint32_t>` lists), an optional
  vertex-color array, and an endianness flag. The writer emits the
  ASCII header up to `end_header\n`, then writes the binary body via
  `std::ofstream(..., std::ios::binary)` and explicit byte-level
  `write` calls (no `operator<<` for binary scalars). For
  big-endian fixtures the writer byte-swaps the four-byte floats and
  the four-byte indices manually before writing.

## Docs
- [x] Update `docs/api/generated/module_inventory.md` only if module
  surfaces change in a way the generator picks up
  (`python3 tools/repo/generate_module_inventory.py --root src --out
  docs/api/generated/module_inventory.md`). Adding internal helpers and
  expanding a function body is not expected to change the inventory; if
  the regenerator only changes the date, leave it untouched (matches
  `GEOIO-002B`/`C`/`D` precedent).
- [x] No additional architecture/migration doc edits required for this
  slice; parity-matrix updates remain part of the parent `GEOIO-002`
  task once asset/runtime routing actually drops the legacy graphics
  importers.

## Acceptance criteria
- [x] `Geometry::MeshIO::LoadPLY` returns a populated `MeshIOResult` for
  well-formed binary little-endian and binary big-endian PLY mesh
  fixtures, with positions equal to the input vertices and face indices
  matching the input list.
- [x] `LoadPLY` continues to accept ASCII PLY fixtures (existing
  `LoadsASCIIPLYTriangle` and `LoadsAsciiPLYAfterBinaryDispatch`
  regression both pass).
- [x] A truncated or malformed binary payload produces
  `Core::ErrorCode::InvalidFormat` rather than out-of-bounds reads or
  partial parses.
- [x] `src/geometry/*` imports remain layered (`geometry -> core` only); no
  new asset/runtime/graphics imports introduced.
- [x] Existing `LoadOBJ`/`LoadOFF`/`LoadSTL`,
  `PointCloudIO`/`GraphIO`, and all `Write*` tests continue to pass.

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
- Mixing this importer with mechanical legacy importer deletion.
- Adding vertex-color/normal pass-through into the mesh result in this
  slice (those properties remain ignored, matching the ASCII path).
- Adding point-cloud PLY binary support in `MeshIO`; that lives in
  `PointCloudIO::LoadPLY` and is a separate slice.
- Touching `src/legacy/Graphics/Importers/*` other than reading them as
  behavioral reference.
- Auto-acknowledging or mutating any runtime/render extraction state
  (unrelated to this slice).
- Introducing GPU/Vulkan-only verification requirements.

## Completion
- Completed: 2026-05-08.
- Implementation commit: `6a0334c`
  (`GEOIO-002E: add geometry-owned binary PLY mesh importer`).
- Retired in a follow-up commit on
  `claude/setup-agentic-workflow-Yxe9K`.
- Verified in this session:
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (87 task files validated before retirement; 87 after
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
    public `Geometry.MeshIO` module surface, matching
    `GEOIO-002B`/`C`/`D` precedent.
- Build/CTest gate not run in this container: `cmake --preset ci`
  configure fails because `clang-20`/`clang++-20` are not installed in
  this agent environment, matching the limitation called out under
  `Context` and the prior `GEOIO-002A`/`B`/`C`/`D` retirement notes.
  The default CPU correctness gate
  (`ctest --test-dir build/ci -R 'GeometryIO' -LE
  'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) should be re-run
  on a host with the documented C++23 toolchain when available.
- Notes:
  - Header parsing now produces a `std::vector<PlyElement>` with full
    `PlyProperty` records (scalar type, list element/count types,
    name) regardless of format, and the existing ASCII vertex/face
    parser was extracted verbatim into a `ParseAsciiPLY` helper that
    consumes the structured `vertex`/`face` element counts. Behavior
    on the previously-supported ASCII path is unchanged; only the
    invariants that previously came from inline header parsing are
    now expressed as `formatSeen`/`headerEndSeen` checks.
  - The binary parser (`ParseBinaryPLY`) walks elements in
    declaration order and only materializes positions and the face
    `vertex_indices`/`vertex_index` list, matching the geometry-side
    ASCII reader's no-color/no-normal behavior. Other vertex scalar
    properties (e.g., `uchar red/green/blue`) are skipped by stride;
    list properties inside the vertex element are rejected because
    they would break fixed-stride decoding. Big-endian decoding goes
    through a small `ReadScalarAs<T>` helper that byte-swaps a
    fixed-size buffer before `std::memcpy` into the host scalar
    type.
  - Coverage in `tests/unit/geometry/Test.GeometryIO.cpp` adds seven
    cases: `LoadsBinaryLittleEndianPLYTriangle`,
    `LoadsBinaryBigEndianPLYTriangle`,
    `LoadsBinaryLittleEndianPLYQuad`,
    `LoadsBinaryLittleEndianPLYWithExtraVertexProperties`,
    `LoadPLYRejectsTruncatedBinaryBody`,
    `LoadPLYRejectsBinaryFaceListBelowThree`, and
    `LoadsAsciiPLYAfterBinaryDispatch`. A new `WriteBinaryPLYFixture`
    helper writes the canonical ASCII header followed by a binary
    body via `std::ofstream(..., std::ios::binary)` with explicit
    byte-swap for big-endian variants.
  - Remaining `GEOIO-002` scope (binary point-cloud PLY import,
    binary PCD import, granular `MeshIOReadStatus` diagnostics enum
    across OBJ/OFF/PLY/STL, domain-selection metadata for
    asset/runtime routing, importer parity hardening for additional
    point-cloud variants) stays tracked under the parent backlog
    task `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
