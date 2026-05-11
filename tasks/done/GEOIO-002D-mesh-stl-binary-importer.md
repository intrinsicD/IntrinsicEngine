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
- Status: done.
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
- [x] Extend `src/geometry/Geometry.HalfedgeMesh.IO.cpp`:
  - [x] Add `<cstring>` and `<span>` (already included) for `std::memcpy` and
    spans of `std::byte`.
  - [x] Inside the existing anonymous namespace, add two helpers:
    - [x] `bool IsBinarySTL(std::span<const std::byte> data)` matching the
      legacy detection rule documented in Context.
    - [x] `Core::Expected<MeshIOResult> ParseBinarySTL(std::span<const std::byte>
      data, std::string_view absolute_path)` that:
      - [x] Returns `InvalidMeshFormat()` when `data.size() < 84`.
      - [x] Decodes `triCount` via `std::memcpy` from `data.data() + 80`.
      - [x] Returns `InvalidMeshFormat()` when `triCount == 0` or when
        `data.size() < 80 + 4 + triCount * 50`.
      - [x] Iterates `triCount` triangles. For each, decodes three `vec3`
        positions via `std::memcpy` (offsets 12, 24, 36 within the
        50-byte record), pushes them into `vertices`, and pushes a
        `{3i, 3i+1, 3i+2}` triangle into `faces`.
      - [x] Calls `PopulateResult(result, vertices, faces)` and assigns
        `SourcePath`/`BasePath` via `MakePathInfo`.
  - [x] Modify `LoadSTL` to:
    - [x] Read the file once via `ReadTextFile` (already opens binary mode).
    - [x] Form a `std::span<const std::byte>` over the resulting string's
      bytes.
    - [x] Dispatch: `IsBinarySTL(bytes)` → `ParseBinarySTL(bytes, absolute_path)`;
      otherwise fall through to the existing ASCII parser unchanged.
- [x] Do not change importer behavior for OBJ/OFF/PLY, do not introduce new
  module imports, and do not touch any file outside
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp`,
  `tests/unit/geometry/Test.GeometryIO.cpp`, and `tasks/`.
- [x] Public module surface (`Geometry.HalfedgeMesh.IO.cppm`) does not
  change; the inventory should remain identical apart from the
  regeneration date (matches `GEOIO-002B`/`C` precedent).

## Tests
- [x] Add focused `unit;geometry` coverage to
  `tests/unit/geometry/Test.GeometryIO.cpp`:
  - [x] `LoadsBinarySTLSingleTriangle`: build a 134-byte fixture (80-byte
    header + `uint32_t` count = 1 + one 50-byte triangle record encoding
    `(0,0,0)`, `(1,0,0)`, `(0,1,0)`); load and assert
    `ExpectTriangleMeshProperties(*result)` (3 vertices, 1 triangle face
    `{0,1,2}`, first vertex at the origin).
  - [x] `LoadsBinarySTLTwoTriangles`: build a 184-byte fixture with two
    triangles; verify 6 vertices, 2 faces, and that face 1 indices are
    `{3,4,5}` (triangle-soup ordering).
  - [x] `LoadSTLRejectsTruncatedBinaryPayload`: header advertises
    `triCount == 2` but only one triangle's bytes follow; expect
    `Core::ErrorCode::InvalidFormat`. The exact size (134 bytes when
    `triCount == 2` is advertised) is what disqualifies the size-match
    branch and forces the size-shortage check to trigger
    `InvalidMeshFormat()`.
  - [x] `LoadsASCIISTLAfterBinaryDispatch`: regression — ensure that the
    existing ASCII fixture still parses through the new dispatch path
    (the trimmed prefix starts with `solid` and contains `facet`, so
    `IsBinarySTL` returns `false`).
- [x] Helper: add a `WriteBinarySTLFile` test fixture writer that takes a
  `std::span<const std::array<glm::vec3, 3>>` and emits the canonical
  layout via packed `std::memcpy`-style writes (use
  `std::ofstream(..., std::ios::binary)` and explicit byte writes; do
  not rely on `operator<<`).

## Docs
- [x] Update `docs/api/generated/module_inventory.md` only if module surfaces
  changed in a way the generator picks up
  (`python3 tools/repo/generate_module_inventory.py --root src --out
  docs/api/generated/module_inventory.md`). Adding internal helpers and
  expanding a function body is not expected to change the inventory; if
  the regenerator only changes the date, leave it untouched (matches
  `GEOIO-002B`/`C` precedent).
- [x] No additional architecture/migration doc edits required for this
  slice; parity-matrix updates remain part of the parent `GEOIO-002`
  task once asset/runtime routing actually drops the legacy graphics
  importers.

## Acceptance criteria
- [x] `Geometry::MeshIO::LoadSTL` returns a populated `MeshIOResult` for a
  well-formed binary STL fixture, with positions equal to the input
  triangles and face indices in triangle-soup order.
- [x] `LoadSTL` continues to accept ASCII STL fixtures (existing
  `LoadsASCIISTLTriangle` test still passes, plus the new
  `LoadsASCIISTLAfterBinaryDispatch` regression).
- [x] A truncated binary payload produces `Core::ErrorCode::InvalidFormat`
  rather than out-of-bounds reads or partial parses.
- [x] `src/geometry/*` imports remain layered (`geometry -> core` only); no
  new asset/runtime/graphics imports introduced.
- [x] Existing `LoadOBJ`/`LoadOFF`/`LoadPLY`,
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

## Completion
- Completed: 2026-05-08.
- Implementation commit: `5fa504b`
  (`GEOIO-002D: add geometry-owned binary STL mesh importer`).
- Retired in a follow-up commit on
  `claude/setup-agentic-workflow-O37xw`.
- Verified in this session:
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (86 task files validated).
  - `python3 tools/repo/check_layering.py --root src --strict` — no
    layering violations; `geometry` imports remain `geometry -> core`
    only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/repo/generate_module_inventory.py --root src --out
    docs/api/generated/module_inventory.md` — only the regeneration
    date and pre-existing renames from prior commits differ; this
    slice adds internal helpers and expands a function body in the
    existing `Geometry.MeshIO` module without changing the module
    name set, so the inventory was left untouched to avoid mixing
    unrelated drift into this slice (matches `GEOIO-002B`/`C`
    precedent).
- Build/CTest gate not run in this container: `cmake --preset ci`
  configure fails because `clang-20`/`clang++-20` are not installed in
  this agent environment, matching the limitation called out under
  `Context` and the prior `GEOIO-002A`/`B`/`C` retirement notes.
  The default CPU correctness gate
  (`ctest --test-dir build/ci -R 'GeometryIO' -LE
  'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) should be re-run
  on a host with the documented C++23 toolchain when available.
- Notes:
  - `IsBinarySTL` and `ParseBinarySTL` ship as anonymous-namespace
    helpers in `src/geometry/Geometry.HalfedgeMesh.IO.cpp`. The
    public signature of `LoadSTL` is unchanged; the dispatch is a
    `std::span<const std::byte>` view over the existing
    `ReadTextFile` buffer (which already opens in binary mode), so
    no new IO helper was introduced.
  - Binary parse emits raw triangle-soup positions (3*N) and
    triangle face index lists (`{3i, 3i+1, 3i+2}`), matching the
    geometry-side ASCII path's no-dedup behavior.
  - Coverage in `tests/unit/geometry/Test.GeometryIO.cpp` adds four
    cases: `LoadsBinarySTLSingleTriangle`,
    `LoadsBinarySTLTwoTriangles`,
    `LoadSTLRejectsTruncatedBinaryPayload`, and
    `LoadsASCIISTLAfterBinaryDispatch`. A new `WriteBinarySTLFixture`
    helper writes the canonical 80-byte header + `uint32_t` count +
    50-byte triangle records via `std::ofstream(..., std::ios::binary)`.
  - Remaining `GEOIO-002` scope (binary mesh PLY import, granular
    `MeshIOReadStatus` diagnostics enum across OBJ/OFF/PLY/STL,
    domain-selection metadata for asset/runtime routing, importer
    parity hardening for additional point-cloud variants) stays
    tracked under the parent backlog task
    `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
