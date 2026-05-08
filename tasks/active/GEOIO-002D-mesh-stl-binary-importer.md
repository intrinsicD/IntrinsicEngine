# GEOIO-002D — Geometry-owned binary STL mesh importer

## Goal
- Extend `Geometry::MeshIO::LoadSTL` to accept binary STL files in addition
  to the existing ASCII path, closing a parity gap between the geometry-side
  importer and the legacy `Graphics.Importers.STL` reference (which has long
  supported both encodings). This is the next slice of the parent
  `GEOIO-002` parity hardening task and follows the
  `GEOIO-002A`/`B`/`C` exporter trio.

## Non-goals
- No new module surfaces. The public signature of `LoadSTL` stays
  `Core::Expected<MeshIOResult> LoadSTL(std::string_view absolute_path)`.
- No new diagnostic enum (no `MeshIOReadStatus` parallel to
  `MeshIOWriteStatus`). Failures continue to surface as
  `Core::ErrorCode` values; a granular reader-side enum is a separate
  follow-up slice (tentatively `GEOIO-002E`) covering OBJ/OFF/PLY/STL
  uniformly once binary STL has landed.
- No vertex deduplication (raw 3*N triangle-soup positions are emitted, in
  parity with the existing ASCII path; dedup is a HalfedgeMesh repair
  concern, not an IO concern).
- No per-facet normal accumulation onto vertex normals. STL binary stores
  per-facet normals; the geometry-side importer ignores them and relies on
  downstream consumers to recompute when needed (matching the ASCII path,
  which has no normal data at all).
- No legacy `Graphics::STLLoader` registry deletion or rewiring; that
  retirement remains tracked under `GRAPHICS-019` follow-ups.
- No asset registry, runtime scene ingestion, ECS construction, or GPU
  upload work.
- No GPU/Vulkan requirement in the default CPU gate.
- No big-endian support (engine targets are little-endian; binary STL is
  little-endian by spec).

## Context
- Status: in-progress.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-O37xw`.
- Parent backlog task:
  `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
- Predecessor slices:
  - `tasks/done/GEOIO-002A-mesh-obj-exporter.md` introduced
    `MeshIOWriteStatus` and `WriteOBJ`.
  - `tasks/done/GEOIO-002B-mesh-ply-exporter.md` added `WritePLY`.
  - `tasks/done/GEOIO-002C-mesh-stl-exporter.md` added ASCII `WriteSTL`.
- The legacy `Graphics.Importers.STL` parser at
  `src/legacy/Graphics/Importers/Graphics.Importers.STL.cpp` (`IsBinarySTL`
  at line 37, `ParseBinary` at line 67) is the behavioral reference. The
  geometry-side port intentionally drops vertex deduplication and
  post-process steps (normal accumulation, UV generation) because they do
  not belong in the geometry-only IO layer.
- `Geometry::IOText::ReadTextFile` already opens files with
  `std::ios::binary`, so the existing helper is a faithful binary read
  even though its name says "text". Reuse it via a `std::span<const
  std::byte>` view formed from `text->data()`/`text->size()`. This avoids
  introducing a new helper for a single use site.
- Binary STL layout (little-endian): 80-byte header, `uint32_t` triangle
  count, then `triCount` records of 50 bytes each: 12 bytes face normal +
  36 bytes (three `vec3` vertices) + 2 bytes attribute byte count.
  Detection rule (legacy): `triCount` decoded from bytes 80..83, then
  `expected = 80 + 4 + 50 * triCount`; if `expected == data.size()` it is
  binary. Fallback: scan the first 1 KiB of bytes; if the trimmed prefix
  starts with `solid` AND contains `facet`, treat as ASCII; otherwise
  treat as binary if `data.size() >= 84`.
- Container build environment is missing `clang-20` and `libxrandr` dev
  headers, so the standard `cmake --preset ci` configure currently fails
  at GLFW dependency discovery (see `tasks/backlog/bugs/index.md` and
  the prior `GEOIO-002A`/`B`/`C` retirement notes). Report build evidence
  honestly; the focused gate may not run in the agent container.

## Required changes
- Extend `src/geometry/Geometry.HalfedgeMesh.IO.cpp`:
  - Add `<cstring>` and `<span>` (already included) for `std::memcpy` and
    spans of `std::byte`.
  - Inside the existing anonymous namespace, add two helpers:
    - `bool IsBinarySTL(std::span<const std::byte> data)` matching the
      legacy detection rule documented in Context.
    - `Core::Expected<MeshIOResult> ParseBinarySTL(std::span<const std::byte>
      data, std::string_view absolute_path)` that:
      - Returns `InvalidMeshFormat()` when `data.size() < 84`.
      - Decodes `triCount` via `std::memcpy` from `data.data() + 80`.
      - Returns `InvalidMeshFormat()` when `triCount == 0` or when
        `data.size() < 80 + 4 + triCount * 50`.
      - Iterates `triCount` triangles. For each, decodes three `vec3`
        positions via `std::memcpy` (offsets 12, 24, 36 within the
        50-byte record), pushes them into `vertices`, and pushes a
        `{3i, 3i+1, 3i+2}` triangle into `faces`.
      - Calls `PopulateResult(result, vertices, faces)` and assigns
        `SourcePath`/`BasePath` via `MakePathInfo`.
  - Modify `LoadSTL` to:
    - Read the file once via `ReadTextFile` (already opens binary mode).
    - Form a `std::span<const std::byte>` over the resulting string's
      bytes.
    - Dispatch: `IsBinarySTL(bytes)` → `ParseBinarySTL(bytes, absolute_path)`;
      otherwise fall through to the existing ASCII parser unchanged.
- Do not change importer behavior for OBJ/OFF/PLY, do not introduce new
  module imports, and do not touch any file outside
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp`,
  `tests/unit/geometry/Test.GeometryIO.cpp`, and `tasks/`.
- Public module surface (`Geometry.HalfedgeMesh.IO.cppm`) does not
  change; the inventory should remain identical apart from the
  regeneration date (matches `GEOIO-002B`/`C` precedent).

## Tests
- Add focused `unit;geometry` coverage to
  `tests/unit/geometry/Test.GeometryIO.cpp`:
  - `LoadsBinarySTLSingleTriangle`: build a 134-byte fixture (80-byte
    header + `uint32_t` count = 1 + one 50-byte triangle record encoding
    `(0,0,0)`, `(1,0,0)`, `(0,1,0)`); load and assert
    `ExpectTriangleMeshProperties(*result)` (3 vertices, 1 triangle face
    `{0,1,2}`, first vertex at the origin).
  - `LoadsBinarySTLTwoTriangles`: build a 184-byte fixture with two
    triangles; verify 6 vertices, 2 faces, and that face 1 indices are
    `{3,4,5}` (triangle-soup ordering).
  - `LoadSTLRejectsTruncatedBinaryPayload`: header advertises
    `triCount == 2` but only one triangle's bytes follow; expect
    `Core::ErrorCode::InvalidFormat`. The exact size (134 bytes when
    `triCount == 2` is advertised) is what disqualifies the size-match
    branch and forces the size-shortage check to trigger
    `InvalidMeshFormat()`.
  - `LoadsASCIISTLAfterBinaryDispatch`: regression — ensure that the
    existing ASCII fixture still parses through the new dispatch path
    (the trimmed prefix starts with `solid` and contains `facet`, so
    `IsBinarySTL` returns `false`).
- Helper: add a `WriteBinarySTLFile` test fixture writer that takes a
  `std::span<const std::array<glm::vec3, 3>>` and emits the canonical
  layout via packed `std::memcpy`-style writes (use
  `std::ofstream(..., std::ios::binary)` and explicit byte writes; do
  not rely on `operator<<`).

## Docs
- Update `docs/api/generated/module_inventory.md` only if module surfaces
  changed in a way the generator picks up
  (`python3 tools/repo/generate_module_inventory.py --root src --out
  docs/api/generated/module_inventory.md`). Adding internal helpers and
  expanding a function body is not expected to change the inventory; if
  the regenerator only changes the date, leave it untouched (matches
  `GEOIO-002B`/`C` precedent).
- No additional architecture/migration doc edits required for this
  slice; parity-matrix updates remain part of the parent `GEOIO-002`
  task once asset/runtime routing actually drops the legacy graphics
  importers.

## Acceptance criteria
- `Geometry::MeshIO::LoadSTL` returns a populated `MeshIOResult` for a
  well-formed binary STL fixture, with positions equal to the input
  triangles and face indices in triangle-soup order.
- `LoadSTL` continues to accept ASCII STL fixtures (existing
  `LoadsASCIISTLTriangle` test still passes, plus the new
  `LoadsASCIISTLAfterBinaryDispatch` regression).
- A truncated binary payload produces `Core::ErrorCode::InvalidFormat`
  rather than out-of-bounds reads or partial parses.
- `src/geometry/*` imports remain layered (`geometry -> core` only); no
  new asset/runtime/graphics imports introduced.
- Existing `LoadOBJ`/`LoadOFF`/`LoadPLY`,
  `PointCloudIO`/`GraphIO`, and all `Write*` tests continue to pass.

## Verification
```bash
# Focused gate (full configure may fail in containers missing libxrandr / clang-20):
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
- Adding vertex deduplication, normal accumulation, or UV generation in
  this slice.
- Adding big-endian binary STL support, color-extension parsing
  (`uint16_t` attribute byte count is read past, not interpreted), or
  other non-spec extensions.
- Touching `src/legacy/Graphics/Importers/*` other than reading them as
  behavioral reference.
- Auto-acknowledging or mutating any runtime/render extraction state
  (unrelated to this slice).
- Introducing GPU/Vulkan-only verification requirements.
