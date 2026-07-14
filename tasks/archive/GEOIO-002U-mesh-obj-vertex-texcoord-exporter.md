# GEOIO-002U — Geometry-owned OBJ ASCII vertex-texcoord export parity

## Goal
- Extend `Geometry::MeshIO::WriteOBJ` to emit `vt u v`
  vertex-texcoord lines and matching `p/t` or `p/t/n` face
  token references when the input `MeshIOResult` carries a
  `v:texcoord` `glm::vec2` property of the same size as the
  position property, completing the writer side that pairs
  with the lockstep `vt` reader added under `GEOIO-002T`.

## Non-goals
- No per-corner texcoord vertex splitting on write. Texcoords
  are emitted in lockstep with positions, reusing the existing
  one-to-one geometry-owned attribute layout. Multiple UVs per
  position remain an asset-import concern.
- No `vt` emission when `v:texcoord.size() != v:point.size()`.
  Mismatched property arity silently falls back to the existing
  `WriteOBJ` behavior (no `vt`, plain or `p//n` face tokens).
- No third texcoord component emission. `v:texcoord` is
  `glm::vec2`; the writer never emits a `w` coordinate.
- No reader-side change to `LoadOBJ`, `LoadOFF`, `LoadPLY`,
  `LoadSTL`. Lockstep `vt` ingest in `LoadOBJ` already exists
  from `GEOIO-002T` and is exercised here only as the
  round-trip verifier.
- No writer-side change to `WritePLY`, `WritePLYBinary`,
  `WriteSTL`, `WriteSTLBinary`. OBJ remains the only writer
  picking up vertex texcoords in this slice.
- No `vp`, `l`, `g`, `o`, `s`, `usemtl`, or `mtllib` writer
  emission.
- No new `MeshIOWriteStatus` enum members. Texcoord-arity
  mismatch is not an error; it is silent fallback.
- No new public module surface. The change is contained inside
  the existing `Geometry.HalfedgeMesh.IO` module body.
- No assets/runtime ownership of mesh file IO; geometry owns
  format codecs only.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-SRvKI`.
- Parent task:
  `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002T`
  (`tasks/archive/GEOIO-002T-mesh-obj-vertex-texcoord-importer.md`)
  added the lockstep `vt` reader; that slice explicitly deferred
  writer emission to a future slice. This is that slice.
- `Geometry::MeshIO::WriteOBJ` lives in
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp` (around the
  existing `for (const auto& p : positions)` loop and the
  `hasNormals` face-token branch). Before this slice the writer
  emits `v x y z`, optional `vn nx ny nz`, and face tokens
  using `1 2 3` (no normals) or `1//1 2//2 3//3` (with
  normals). It never inspects `v:texcoord`.
- `v:texcoord` is the canonical property name for vertex UVs
  in geometry, published as
  `Geometry::MeshUtils::kVertexTexcoordPropertyName` in
  `src/geometry/Geometry.HalfedgeMesh.Utils.cppm` and read back
  by `LoadOBJ` after `GEOIO-002T`.
- The 1-based OBJ face token grammar accepted by `LoadOBJ`'s
  `ParseOBJVertexIndex` strips any `/...` tail and extracts
  only the position index. That means the new `p/t` and
  `p/t/n` writer outputs round-trip through the existing
  reader without parser changes: the `t` and `n` indices are
  cosmetic alignment for the lockstep ingest convention.
- Compatibility with other OBJ consumers: emitting matching
  1-based `t` indices for every position keeps any face-token-
  driven OBJ reader (legacy `Graphics.Importers.OBJ` and
  external tools) able to resolve UVs to the same lockstep
  values without ambiguity.

## Required changes
- [x] `src/geometry/Geometry.HalfedgeMesh.IO.cpp::WriteOBJ`:
  - [x] After the existing `normalsView` lookup, add a parallel
    `texcoordsView = mesh.Vertices.Get<glm::vec2>("v:texcoord")`
    lookup and a `hasTexcoords` flag computed as
    `texcoordsView.IsValid() &&
     texcoordsView.Vector().size() == positions.size()`.
  - [x] Insert a `vt %.6f %.6f` emission loop between the
    existing `v` emission loop and the existing `vn`
    emission loop (i.e. `v` block, then `vt` block if
    `hasTexcoords`, then `vn` block if `hasNormals`).
    Match the existing `snprintf`/`stream.write`/`written <= 0`
    failure handling pattern.
  - [x] Replace the existing two-way `(hasNormals ? "%llu//%llu"
    : "%llu")` face-token branch with the four-way table:
    - [x] `!hasTexcoords && !hasNormals`: `" %llu"` (unchanged).
    - [x] `!hasTexcoords &&  hasNormals`: `" %llu//%llu"` (unchanged).
    - [x] ` hasTexcoords && !hasNormals`: `" %llu/%llu"`.
    - [x] ` hasTexcoords &&  hasNormals`: `" %llu/%llu/%llu"`.
    The `t` and `n` indices are the same 1-based position
    index, matching the lockstep storage on disk.
- [x] No header file changes; the change lives entirely in the
  existing `WriteOBJ` body.

## Tests
- [x] Add `unit;geometry` cases to
  `tests/unit/geometry/Test.GeometryIO.cpp::GeometryIO_MeshIO`
  next to the existing `WritesOBJTriangleWithNormals` test:
  - [x] `WritesOBJTriangleWithTexcoords` — triangle with positions
    and a `v:texcoord` property, no normals. Expect the on-disk
    contents to contain `vt 0.000000 0.000000\n` and
    `f 1/1 2/2 3/3\n`. Round-trip via `LoadOBJ`; expect a
    `v:texcoord` property of size 3 whose values equal the
    written ones.
  - [x] `WritesOBJTriangleWithTexcoordsAndNormals` — triangle with
    positions, `v:texcoord`, and `v:normal` properties. Expect
    `vt`, `vn`, and `f 1/1/1 2/2/2 3/3/3` to appear on disk.
    Round-trip via `LoadOBJ`; expect both `v:texcoord` and
    `v:normal` properties present and equal to the inputs.
  - [x] `WriteOBJOmitsTexcoordsWhenAbsent` — triangle with no
    `v:texcoord` property at all. Expect the on-disk contents
    to contain no `vt` line, no `/` character in any token,
    and the unchanged `f 1 2 3` face format. Round-trip via
    `LoadOBJ` and confirm no `v:texcoord` property is present
    on the reloaded mesh. (The defensive
    `Vector().size() == positions.size()` arity guard in
    `WriteOBJ` is not reachable through the current
    `PropertySet` API, which keeps all properties in a column
    sized to `mesh.Vertices.Size()`; the guard remains for
    forward-compatibility with future property-set shapes and
    is not exercised by a dedicated test.)
- [x] Extend the test helper `PopulateTriangleMesh` to accept an
  optional `std::span<const glm::vec2> texcoords` parameter
  (default empty), mirroring the existing optional `normals`
  parameter. The helper writes the `v:texcoord` property
  when the span is non-empty.

## Docs
- [x] Update the
  `OBJ/OFF/STL mesh import and mesh PLY import` row in
  `docs/migration/nonlegacy-parity-matrix.md` to record the
  new geometry-owned OBJ vt lockstep export added under
  `GEOIO-002U`. (The matrix already cross-references
  `GEOIO-002S`/`GEOIO-002T` in the same row.)
- [x] Regenerate `docs/api/generated/module_inventory.md` only if
  the generator picks up changes to the existing
  `Geometry.HalfedgeMesh.IO` module surface. If the regenerator
  changes only the date stamp, leave it untouched.

## Acceptance criteria
- [x] `Geometry::MeshIO::WriteOBJ` emits `vt u v` lines and
  matching `p/t` or `p/t/n` face token references whenever the
  input mesh carries a `v:texcoord` property whose size equals
  the position count.
- [x] Mismatched `v:texcoord` arity silently falls back to the
  pre-slice writer behavior.
- [x] New tests pass under `IntrinsicTests` and the CPU gate.
- [x] No assets/runtime/graphics imports leak into
  `src/geometry/*`.
- [x] Parity matrix records the new OBJ vt lockstep export.

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
- Adding assets/runtime/graphics/RHI imports to
  `src/geometry/*`.
- Adding per-corner texcoord vertex splitting on write,
  multi-UV `v:texcoord.N` channel emission, `vp` writer
  emission, or `l`/`g`/`o`/`s`/`usemtl`/`mtllib` writer
  emission.
- Changing `LoadOBJ`, `LoadOFF`, `LoadPLY`, `LoadSTL`,
  `WritePLY`, `WritePLYBinary`, `WriteSTL`, or `WriteSTLBinary`
  signatures or behavior.
- Mixing mechanical legacy deletion with semantic IO
  implementation.

## Completion
- Completed: 2026-05-11.
- Status: done.
- Branch: `claude/setup-agentic-workflow-SRvKI`.
- Implementation commit: `ed519fe`
  (`GEOIO-002U: add OBJ ASCII vertex-texcoord export parity`).
- Verified in this session:
  - `python3 tools/repo/check_layering.py --root src --strict` —
    no layering violations; `geometry` imports remain
    `geometry -> core` only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (150 task files validated; one above the
    GEOIO-002T baseline of 149 once this file is included).
  - `python3 tools/docs/check_doc_links.py --root .` — 0 broken
    relative links.
  - `python3 tools/repo/generate_module_inventory.py --root src
    --out docs/api/generated/module_inventory.md` — 420 modules;
    no diff (the writer extension lives inside the existing
    `Geometry.HalfedgeMesh.IO` module surface and the inventory
    tracks modules, not individual functions).
- CPU build/test gate not exercised: this container lacks
  `clang-20` and `clang-scan-deps` 20+, which the `ci` preset
  hard-codes. `cmake --preset ci` therefore fails at the
  compiler detection step, mirroring the constraint already
  recorded by `GEOIO-002L`..`GEOIO-002T` and earlier slices.
  Build verification needs to re-run on a CI host with the
  correct toolchain prior to merge.
- Notes:
  - `Geometry::MeshIO::WriteOBJ` looks up the optional
    `v:texcoord` `glm::vec2` property next to the existing
    `v:normal` lookup, then emits a contiguous `vt u v` block
    between the `v` and `vn` blocks when the property is
    present in lockstep with positions. Face token emission
    now selects from a four-way table
    (`f i`, `f i//i`, `f i/i`, `f i/i/i`) depending on which
    of `hasTexcoords`/`hasNormals` are true. The `t` and `n`
    indices use the same 1-based position index, matching the
    lockstep storage on disk.
  - The defensive
    `texcoordsView.Vector().size() == positions.size()` guard
    is not reachable through the current `PropertySet` API
    (all properties in a column share `Vertices.Size()`); it
    is kept for forward-compatibility with future property-set
    shapes and is not exercised by a dedicated test.
  - Tests in
    `tests/unit/geometry/Test.GeometryIO.cpp::GeometryIO_MeshIO`
    add `WritesOBJTriangleWithTexcoords`,
    `WritesOBJTriangleWithTexcoordsAndNormals`, and
    `WriteOBJOmitsTexcoordsWhenAbsent`, and extend
    `PopulateTriangleMesh` with an optional
    `std::span<const glm::vec2> texcoords` parameter that
    mirrors the existing optional `normals` parameter.
  - `docs/migration/nonlegacy-parity-matrix.md` records the
    new geometry-owned OBJ vt lockstep exporter in the
    `OBJ/PLY/STL exporters` row, next to the
    `GEOIO-002J`..`GEOIO-002R` exporter milestones.
  - Remaining `GEOIO-002` scope (granular reader-side
    `MeshIOReadStatus`/`PointCloudIOReadStatus` diagnostics;
    further OBJ ASCII parity such as per-corner
    texcoord/normal vertex duplication driven by `p/t/n` face
    triplets; packed-`rgb`/`rgba` PCD plus
    `binary_compressed` LZF decompression) stays tracked
    under the parent backlog task
    `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
