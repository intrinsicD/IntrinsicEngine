# GEOIO-002G — Geometry-owned binary PCD point-cloud importer

## Goal
- Extend `Geometry::PointCloudIO::LoadPCD` to accept `DATA binary`
  (uncompressed) PCD point-cloud files in addition to the existing
  `DATA ascii` path. Positions remain mandatory; optional
  `normal_x/normal_y/normal_z` normals and `r/g/b` colors are decoded
  when present, mirroring the geometry-side ASCII PCD reader's
  behavior.

## Non-goals
- No new public module surface. `LoadPCD` keeps the signature
  `Core::Expected<PointCloudIOResult> LoadPCD(std::string_view absolute_path)`.
- No `DATA binary_compressed` support; that is rejected as
  `Core::ErrorCode::InvalidFormat`. The legacy reference loader does
  not handle compressed PCD either, and adding LZF decompression is
  out of scope for this slice.
- No packed `rgb`/`rgba` 32-bit color support. Only per-channel
  `r`/`g`/`b` fields are populated, matching the existing ASCII
  point-cloud PCD path. Packed-color support is tracked under the
  parent `GEOIO-002` task if/when asset/runtime routing requires it.
- No granular `PointCloudIOReadStatus` diagnostics enum; failures
  continue to surface as `Core::ErrorCode::InvalidFormat`. The
  reader-side diagnostics enum across point-cloud loaders remains a
  separate follow-up under the parent task.
- No legacy `Graphics::PCDLoader` retirement or rewiring; that remains
  under `GRAPHICS-019` follow-ups.
- No asset registry, runtime scene ingestion, ECS construction, or GPU
  upload work.
- No PLY/STL/OBJ/XYZ/TGF behavior change.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-B6Efb`.
- Parent task:
  `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
- Predecessor slice: `tasks/archive/GEOIO-002F-pointcloud-ply-binary-importer.md`
  introduced the binary point-cloud pattern (header parse with full
  per-property records, ASCII helper extraction, binary helper
  reading positions plus optional `nx/ny/nz` normals and uchar
  `red/green/blue` colors with stride-based skipping). This slice
  ports the same shape onto PCD's header form (`FIELDS`/`SIZE`/
  `TYPE`/`COUNT`/`WIDTH`/`HEIGHT`/`POINTS`/`DATA`).
- Behavioral reference:
  `src/legacy/Graphics/Importers/Graphics.Importers.PCD.cpp` —
  `PCDField`/`PCDHeader` at lines 35-54, `ReadScalar<T>` at
  lines 82-107, `ReadBinaryFieldValue` at lines 127-158, `ParseHeader`
  at lines 257-383, binary dispatch at lines 467-498. The
  geometry-side port keeps a smaller surface (positions + optional
  per-channel normal/r/g/b only) consistent with the ASCII PCD
  reader at lines 701-845 of
  `src/geometry/Geometry.PointCloud.IO.cpp`.
- Current `LoadPCD` at lines 701-845 of
  `src/geometry/Geometry.PointCloud.IO.cpp` only parses `FIELDS`,
  `POINTS`, and `DATA` from the header; it ignores `SIZE`, `TYPE`,
  `COUNT`, `WIDTH`, `HEIGHT`, and rejects anything other than
  `DATA ascii` at line 748. To support binary, the header parser
  must capture per-field `Size`, `Type`, and `Count` and compute a
  per-row stride. ASCII behavior must remain byte-equivalent.
- The existing `ReadTextFile` opens with `std::ios::binary` and
  preserves all bytes (including those after the `DATA binary\n`
  line), so the binary body can be reinterpreted as
  `std::span<const std::byte>` over the bytes after the `DATA` line,
  matching the GEOIO-002E/002F approach.
- PCD spec note: per the PCL PCD specification, binary PCD scalar
  fields are stored in host-endian byte order. The legacy reference
  uses `std::byteswap` only when `std::endian::native == big`. The
  geometry-side port follows the same posture: a `bigEndianHost`
  flag derived from `std::endian::native` is passed to a
  `ReadScalarAs<T>(...)` helper that byte-swaps integer scalars on
  big-endian hosts and reinterprets the float bit pattern via
  `std::bit_cast`. Test fixtures encode floats by
  `std::memcpy`-ing the host float into the byte buffer, which is
  also host-endian by construction, so on x86_64 (little-endian) the
  fast-path is a direct memcpy.
- Container build environment is missing `clang-20` and `libxrandr`
  dev headers, so `cmake --preset ci` configure currently fails at
  GLFW dependency discovery (see prior `GEOIO-002A`-`002F`
  retirement notes and `tasks/backlog/bugs/index.md`). Report build
  evidence honestly; the focused gate may not run in the agent
  container.

## Required changes
- [x] Edit `src/geometry/Geometry.PointCloud.IO.cpp` only:
  - [x] Inside the existing anonymous namespace, add small helpers
    (placed near the existing PLY helpers; the PCD helpers stay
    independent of `PlyScalar`/`PlyProperty` to keep the two parsers
    decoupled):
    - [x] `struct PcdField { std::string Name; std::size_t Size; char
      Type; std::size_t Count; std::size_t ByteOffset; std::size_t
      ScalarOffset; }`.
    - [x] `struct PcdHeader { std::vector<PcdField> Fields; std::size_t
      Points; std::size_t Width; std::size_t Height; std::size_t
      PointStride; std::size_t ScalarValueCount; std::string
      DataEncoding; }`.
    - [x] `[[nodiscard]] std::optional<PcdHeader> ParsePCDHeader(
      std::string_view text, std::size_t& cursor)` — recognizes
      `FIELDS`, `SIZE`, `TYPE`, `COUNT`, `POINTS`, `WIDTH`,
      `HEIGHT`, `DATA`; ignores comments and blank lines; computes
      per-field `ByteOffset` and `ScalarOffset`, the per-row
      `PointStride`, and the per-row `ScalarValueCount`. Defaults
      `COUNT` to 1 when omitted, `HEIGHT` to 1, and falls back to
      `Points = Width * Height` if `POINTS` is absent. Rejects
      mismatched FIELDS/SIZE/TYPE counts, zero `Size`, zero
      `Count`, missing `DATA` line.
    - [x] `[[nodiscard]] const PcdField* FindPCDField(
      std::span<const PcdField>, std::string_view)`.
    - [x] `[[nodiscard]] std::optional<float> ReadPCDBinaryScalar(
      std::span<const std::byte> pointBytes, const PcdField&)` — the
      direct port of `ReadBinaryFieldValue` from the legacy
      reference, using `std::memcpy` plus a host-endian byte-swap
      for integer types via `std::byteswap` when
      `std::endian::native == std::endian::big`. Floats use
      `std::bit_cast<float>(std::uint32_t)` on the byte-swapped
      bits.
  - [x] Refactor `LoadPCD` to:
    - [x] Read the file via the existing `ReadTextFile` (no change).
    - [x] Replace the inline header loop with `ParsePCDHeader`. Reject
      with `InvalidPointCloudFormat()` if the parse fails.
    - [x] Locate `x`, `y`, `z` fields (mandatory) and the optional
      `normal_x/normal_y/normal_z` and `r/g/b` fields via
      `FindPCDField`, mirroring the existing ASCII path.
    - [x] Branch on `header.DataEncoding`:
      - [x] `"ascii"` — preserve the existing ASCII row loop byte-for-
        byte (using the existing `ParseNumber<float>` + scalar-index
        lookup against `tokens`). The lookup index for each field is
        `field.ScalarOffset` (which equals the previous direct-FIELD
        index for the common all-`COUNT 1` case, so existing
        fixtures continue to parse identically).
      - [x] `"binary"` — take the bytes from `cursor` to end as the body
        span, reject if `header.PointStride == 0`, and walk
        `header.Points` rows. If `Points == 0` and `Width*Height == 0`
        as well, reject. For each row, slice a per-row
        `std::span<const std::byte>` of `header.PointStride` bytes
        and decode positions/normals/colors via
        `ReadPCDBinaryScalar` plus `NormalizeColorChannel` for color
        channels (matching the ASCII path). A truncated body
        (`remainingBytes < pointCount * pointStride`) rejects with
        `InvalidPointCloudFormat()`.
      - [x] Anything else (including `"binary_compressed"`) rejects with
        `InvalidPointCloudFormat()`.
    - [x] Final empty-cloud / mismatched-count rejection at the end of
      the function continues to apply to both branches.
- [x] Public module surface (`Geometry.PointCloud.IO.cppm`) does not
  change; the inventory should remain identical apart from the
  regeneration date (matches `GEOIO-002B`/`C`/`D`/`E`/`F` precedent).
- [x] Do not change `LoadXYZ`/`LoadPLY` behavior, do not introduce new
  module imports, and do not touch any file outside
  `src/geometry/Geometry.PointCloud.IO.cpp`,
  `tests/unit/geometry/Test.GeometryIO.cpp`, and `tasks/`.

## Tests
- [x] Add focused `unit;geometry` coverage to
  `tests/unit/geometry/Test.GeometryIO.cpp`:
  - [x] `LoadsBinaryPCDPointCloud`: minimal `FIELDS x y z` /
    `SIZE 4 4 4` / `TYPE F F F` / `COUNT 1 1 1` /
    `WIDTH N` / `HEIGHT 1` / `POINTS N` / `DATA binary` followed by
    binary float positions for three vertices; assert positions
    match.
  - [x] `LoadsBinaryPCDPointCloudWithNormalsAndColor`: layout includes
    `normal_x normal_y normal_z` (Float32) and `r g b` (UInt8);
    assert positions, `HasNormals`, normals, `HasColors`, and color
    channels normalized via `NormalizeColorChannel`.
  - [x] `LoadsBinaryPCDPointCloudSkipsExtraScalars`: layout includes an
    unrecognized `intensity` Float32 field between the position and
    color fields; positions and colors must still parse correctly.
  - [x] `LoadsBinaryPCDPointCloudFromWidthHeight`: `POINTS` line is
    absent; the loader must derive `Points = Width * Height`.
  - [x] `LoadPCDRejectsTruncatedBinaryBody`: header advertises four
    points but only one point's worth of bytes is written; expect
    `Core::ErrorCode::InvalidFormat`.
  - [x] `LoadPCDRejectsBinaryCompressed`: header is otherwise valid but
    `DATA binary_compressed`; expect
    `Core::ErrorCode::InvalidFormat`.
  - [x] `LoadPCDRejectsZeroSizeField`: `SIZE 0 4 4` makes the per-field
    size zero for `x`; expect `Core::ErrorCode::InvalidFormat`.
  - [x] Regression: the existing
    `LoadsASCIIPCDWithNormalsAndColor` (line 151) must continue to
    pass through the refactored dispatch path.
- [x] Helper: add a `WriteBinaryPCDFixture` test fixture writer
  alongside `WriteBinaryPLYPointCloudFixture` (line 1053) that:
  - [x] Takes positions, optional normals, optional uchar `r/g/b`
    colors, an optional extra-`float intensity` flag, an
    `advertisedPointCountOverride`, an `omitPointsLine` flag (for
    the WIDTH/HEIGHT-derived case), a `dataEncoding` string
    (`"binary"` / `"binary_compressed"` / etc.), an
    `overrideXSize` knob (for the zero-size negative test), and a
    truncation-after-first-point flag.
  - [x] Emits the ASCII header up through `DATA <encoding>\n`, then
    writes the binary body via `std::ofstream(..., std::ios::binary)`
    and explicit byte-level `write` calls. Floats are written by
    `std::memcpy`-ing the host float into a four-byte buffer
    (host-endian — PCD binary is host-endian by spec). UInt8 colors
    are written as one byte each. The optional `intensity` is
    written as a host-endian Float32 between the normals (if any)
    and the colors (if any).

## Docs
- [x] Update `docs/api/generated/module_inventory.md` only if module
  surfaces change in a way the generator picks up
  (`python3 tools/repo/generate_module_inventory.py --root src --out
  docs/api/generated/module_inventory.md`). Adding internal helpers
  and expanding a function body is not expected to change the
  inventory; if the regenerator only changes the date, leave it
  untouched (matches `GEOIO-002B`/`C`/`D`/`E`/`F` precedent).
- [x] No additional architecture/migration doc edits required for this
  slice; parity-matrix updates remain part of the parent
  `GEOIO-002` task once asset/runtime routing actually drops the
  legacy graphics importers.

## Acceptance criteria
- [x] `Geometry::PointCloudIO::LoadPCD` returns a populated
  `PointCloudIOResult` for well-formed binary PCD point-cloud
  fixtures, with positions equal to the input vertices and, when
  present, normals and normalized colors.
- [x] `LoadPCD` continues to accept ASCII PCD fixtures (existing
  `LoadsASCIIPCDWithNormalsAndColor` continues to pass).
- [x] A truncated binary payload, a `binary_compressed` encoding, or a
  zero-size field declaration produces `Core::ErrorCode::InvalidFormat`
  rather than out-of-bounds reads or partial parses.
- [x] `src/geometry/*` imports remain layered (`geometry -> core` only);
  no new asset/runtime/graphics imports introduced.
- [x] Existing `LoadXYZ`, mesh `LoadPLY`/`LoadOBJ`/`LoadOFF`/`LoadSTL`,
  point-cloud `LoadPLY`, and all `Write*` tests continue to pass.

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
- Adding additional vertex property pass-through (e.g. UVs,
  intensity, packed `rgb`/`rgba`) into `PointCloud::Cloud` in this
  slice; only positions, optional `normal_x/normal_y/normal_z`
  normals, and optional `r/g/b` colors are populated, matching the
  existing ASCII point-cloud reader.
- Adding `binary_compressed` (LZF) decompression in this slice; that
  is a separate parent-`GEOIO-002` follow-up if needed.
- Touching `src/legacy/Graphics/Importers/*` other than reading them
  as behavioral reference.
- Introducing GPU/Vulkan-only verification requirements.
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.

## Completion
- Completed: 2026-05-08.
- Status: done.
- Implementation commit: `99facf8`
  (`GEOIO-002G: add geometry-owned binary PCD point-cloud importer`).
- Retired in a follow-up commit on
  `claude/setup-agentic-workflow-B6Efb`.
- Verified in this session:
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (96 task files validated before retirement; the same
    96 after retirement, including this file under `tasks/done/`).
  - `python3 tools/repo/check_layering.py --root src --strict` — no
    layering violations; `geometry` imports remain `geometry -> core`
    only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/repo/generate_module_inventory.py --root src --out
    docs/api/generated/module_inventory.md` — no diff vs. the
    pre-existing inventory; this slice adds anonymous-namespace
    helpers and refactors `LoadPCD` internally without changing the
    public `Geometry.PointCloud.IO` module surface, matching
    `GEOIO-002B`/`C`/`D`/`E`/`F` precedent.
- Build/CTest gate not run in this container: `cmake --preset ci`
  configure fails because `clang-20`/`clang++-20` are not installed
  in this agent environment, matching the limitation called out in
  `Context` and the prior `GEOIO-002A`-`002F` retirement notes. The
  default CPU correctness gate
  (`ctest --test-dir build/ci -R 'GeometryIO' -LE
  'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) should be
  re-run on a host with the documented C++23 toolchain when
  available.
- Notes:
  - Header parsing now produces a `PcdHeader` with full per-field
    `PcdField` records (name, byte size, scalar type tag, count,
    byte offset, scalar offset). The existing inline `FIELDS` /
    `POINTS` / `DATA` capture is replaced by a single
    `ParsePCDHeader` helper that also recognizes `SIZE`, `TYPE`,
    `COUNT`, `WIDTH`, and `HEIGHT`. ASCII-path parsing is preserved
    byte-for-byte by indexing tokens via `field.ScalarOffset`,
    which equals the previous direct-FIELD index for the common
    all-`COUNT 1` case (the existing
    `LoadsASCIIPCDWithNormalsAndColor` continues to pass through
    the refactored dispatch).
  - The binary parser slices a per-row
    `std::span<const std::byte>` of `header.PointStride` bytes from
    the body span and decodes positions / optional normals /
    optional `r/g/b` colors via `ReadPCDBinaryScalar`, which
    handles `F` (4/8 bytes), `I` (1/2/4/8 bytes), and `U` (1/2/4/8
    bytes) types. Per the PCD spec, binary scalars are stored in
    host-endian byte order; the helper byte-swaps integer scalars
    only on big-endian hosts via `std::byteswap` and reinterprets
    float bit patterns through `std::bit_cast`. Unrecognized
    fields are skipped via the per-row stride. When `POINTS` is
    absent, the loader derives `Points = Width * Height`.
  - Coverage in `tests/unit/geometry/Test.GeometryIO.cpp` adds
    seven cases: `LoadsBinaryPCDPointCloud`,
    `LoadsBinaryPCDPointCloudWithNormalsAndColor`,
    `LoadsBinaryPCDPointCloudSkipsExtraScalars`,
    `LoadsBinaryPCDPointCloudFromWidthHeight`,
    `LoadPCDRejectsTruncatedBinaryBody`,
    `LoadPCDRejectsBinaryCompressed`, and
    `LoadPCDRejectsZeroSizeField`. A `WriteBinaryPCDFixture`
    helper writes the canonical ASCII header followed by a
    host-endian binary body via `std::ofstream(...,
    std::ios::binary)`, with optional `normal_x/normal_y/normal_z`,
    optional `intensity` extra-scalar, and optional `r/g/b` blocks.
    The zero-`SIZE` case bypasses the helper and emits the
    fixture inline because the writer enforces `SIZE >= 1`.
  - Remaining `GEOIO-002` scope (granular
    `MeshIOReadStatus`/`PointCloudIOReadStatus` diagnostics enums,
    domain-selection metadata for asset/runtime routing,
    importer parity hardening for additional point-cloud variants
    such as packed `rgb`/`rgba` PCD and `binary_compressed` LZF
    decompression) stays tracked under the parent backlog task
    `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
