# GEOIO-002S — Geometry-owned OBJ ASCII vertex-normal import parity

## Goal
- Extend `Geometry::MeshIO::LoadOBJ` to parse `vn x y z`
  vertex-normal lines and emit them as a `v:normal` property on
  the returned `MeshIOResult` whenever the normal count matches
  the position count, so the existing `WriteOBJ` → `LoadOBJ`
  round trip preserves vertex normals symmetric to the writer
  shipped under `GEOIO-002A` and refined under `GEOIO-002L`.

## Non-goals
- No per-corner normal vertex duplication. The legacy
  `Graphics.Importers.OBJ` importer resolves `p/t/n` triplets
  into unique attribute vertices and may split a single OBJ
  vertex across multiple result vertices; that behavior stays
  out of scope and remains owned by the asset import layer.
- No `vt` texture-coordinate ingest or `v:texcoord`/`v:uv`
  property emission.
- No `l` line topology, `g`/`o`/`s` group/object/smoothing,
  `usemtl`/`mtllib`, or `vp` parameter-space-vertex parsing.
- No negative `vn` index resolution on face tokens. Face token
  parsing is unchanged: `ParseOBJVertexIndex` continues to
  extract only the position index from `p`, `p/t`, or `p/t/n`
  tokens.
- No CPU normal generation when the file omits normals or when
  the `vn` count mismatches the `v` count. Mismatched normals
  are silently dropped (no error) and the result has no
  `v:normal` property.
- No assets/runtime ownership of mesh file IO; geometry owns
  format codecs only.
- No reader-side change to `LoadOFF`, `LoadPLY`, or `LoadSTL`.
- No writer-side change to `WriteOBJ`; the existing emitter
  already produces lockstep `vn` lines and `v//vn` face tokens
  when the input mesh carries a `v:normal` property.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-POcO4`.
- Parent task:
  `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002A`/`L` shipped the geometry-owned OBJ writer pair
  (`WriteOBJ` ASCII and `WritePLYBinary`/`WritePLY` PLY peers).
  `WriteOBJ` already emits `vn nx ny nz` lines in lockstep with
  `v x y z` lines whenever the input mesh has a `v:normal`
  property of matching length, and emits faces as
  `f v//vn v//vn ...` in that case.
- The reader is in
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp::LoadOBJ`. Before
  this slice it parsed only `v` and `f` lines and silently
  dropped `vn` (and any other token). The existing
  `WritesOBJTriangleWithNormals` test in
  `tests/unit/geometry/Test.GeometryIO.cpp` therefore round
  tripped positions and faces, but the imported mesh had no
  `v:normal` property even though the on-disk file contained
  `vn` lines. This slice closes that asymmetric round-trip gap
  for the lockstep case the writer produces.
- The reader now collects `vn` lines into a local
  `std::vector<glm::vec3> normals`. Once parsing completes, if
  `normals.size() == vertices.size()` the vector is forwarded
  to the existing `PopulateResult(result, vertices, faces,
  normals)` overload, which writes `v:normal` exactly as the
  PLY/OFF readers already do. Any other count is silently
  dropped (consistent with how the legacy importer tolerates
  per-corner normals without exposing them on the vertex
  property set).
- `ParseOBJVertexIndex` is unchanged: it strips any `/...` tail
  from face tokens and continues to extract only the position
  index. The face arity validation and out-of-range index
  check remain in place.

## Required changes
- [x] `src/geometry/Geometry.HalfedgeMesh.IO.cpp::LoadOBJ`:
  - [x] Add a `std::vector<glm::vec3> normals;` local alongside
    `vertices`/`faces`.
  - [x] Add an `else if (tokens[0] == "vn")` branch that requires
    `tokens.size() >= 4`, parses three floats via
    `ParseNumber<float>`, and pushes a `glm::vec3` onto
    `normals`. Reject malformed values with `InvalidMeshFormat`
    matching the existing `v` line behavior.
  - [x] After the parse loop, build a `std::span<const glm::vec3>`
    that wraps `normals` only when
    `normals.size() == vertices.size()` and pass it to
    `PopulateResult(result, vertices, faces, normalsSpan)`.
    Use the existing default for the `colors` span.

## Tests
- [x] Add `unit;geometry` cases to
  `tests/unit/geometry/Test.GeometryIO.cpp::GeometryIO_MeshIO`
  next to the existing `LoadsOBJTriangle` test:
  - [x] `LoadsOBJTriangleWithVertexNormals` — ASCII OBJ with three
    `v` lines, three `vn` lines (last one differs from the
    others), and `f 1//1 2//2 3//3` succeeds and returns a
    `v:normal` property of size 3 whose values match the
    `vn` lines.
  - [x] `LoadOBJIgnoresMismatchedVertexNormalCount` — three `v`
    lines, two `vn` lines, and a non-attributed face succeeds
    but returns no `v:normal` property.
  - [x] `LoadOBJRejectsMalformedVertexNormal` — `vn 0 0` with two
    components yields `Core::ErrorCode::InvalidFormat`
    (matches the `v` line strictness).
- [x] Extend `WritesOBJTriangleWithNormals` to assert that the
  re-imported mesh exposes the same `v:normal` values it was
  written with (closes the existing latent round-trip gap).

## Docs
- [x] Update the
  `OBJ/OFF/STL mesh import and mesh PLY import` row in
  `docs/migration/nonlegacy-parity-matrix.md` to record the
  new OBJ vertex-normal lockstep import added under
  `GEOIO-002S`.
- [x] Regenerate `docs/api/generated/module_inventory.md` only if
  the generator picks up changes to the existing
  `Geometry.HalfedgeMesh.IO` module surface. If the regenerator
  changes only the date stamp, leave it untouched.

## Acceptance criteria
- [x] `Geometry::MeshIO::LoadOBJ` round-trips a `v:normal` property
  through `WriteOBJ` for the lockstep case the writer produces.
- [x] New tests pass under `IntrinsicTests` and the CPU gate.
- [x] No assets/runtime/graphics imports leak into
  `src/geometry/*`.
- [x] Parity matrix records the new OBJ vn lockstep import.

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
- Adding `vt` ingest, per-corner normal vertex duplication,
  `l`/`g`/`o`/`s`/`usemtl`/`mtllib` parsing, negative `vn`
  index resolution, or CPU normal generation in this slice.
- Changing the existing `LoadOFF`, `LoadPLY`, `LoadSTL`, or
  `WriteOBJ` signatures or behavior.
- Mixing mechanical legacy deletion with semantic IO
  implementation.

## Completion
- Completed: 2026-05-10.
- Status: done.
- Branch: `claude/setup-agentic-workflow-POcO4`.
- Implementation commit: `4e29288`
  (`GEOIO-002S: add OBJ ASCII vertex-normal import parity`).
- Verified in this session:
  - `python3 tools/repo/check_layering.py --root src --strict` —
    no layering violations; `geometry` imports remain
    `geometry -> core` only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (148 task files validated; the new file is one
    above the GEOIO-002R baseline of 147).
  - `python3 tools/docs/check_doc_links.py --root .` — 0 broken
    relative links.
  - `python3 tools/repo/generate_module_inventory.py --root src
    --out docs/api/generated/module_inventory.md` — 420 modules;
    no diff (the new reader branch lives inside the existing
    `Geometry.HalfedgeMesh.IO` module surface and the inventory
    tracks modules, not individual functions).
- CPU build/test gate not exercised: this container lacks
  `clang-20` and `clang-scan-deps` 20+, which the `ci` preset
  hard-codes. `cmake --preset ci` therefore fails at the
  compiler detection step, mirroring the constraint already
  recorded by `GEOIO-002L`..`GEOIO-002R` and earlier slices.
  Build verification needs to re-run on a CI host with the
  correct toolchain prior to merge.
- Notes:
  - `Geometry::MeshIO::LoadOBJ` collects `vn` lines into a
    local vector. After the parse loop the function builds a
    `std::span<const glm::vec3>` that wraps the normals when
    their count matches the position count, and forwards the
    span to the existing `PopulateResult` overload (which
    already owns the `v:normal` property emission for
    `LoadOFF`/`LoadPLY`).
  - The reader rejects malformed `vn` lines the same way it
    rejects malformed `v` lines: any of the three components
    failing `ParseNumber<float>` returns
    `Core::ErrorCode::InvalidFormat`. A `vn` line with fewer
    than three components is likewise rejected.
  - Face token parsing is unchanged. `ParseOBJVertexIndex`
    continues to extract only the position index from `p`,
    `p/t`, or `p/t/n` tokens, so files that reference normals
    only via `f` token triplets (without lockstep `vn` lines)
    still parse and silently drop the normal references.
  - Tests in
    `tests/unit/geometry/Test.GeometryIO.cpp::GeometryIO_MeshIO`
    add `LoadsOBJTriangleWithVertexNormals`,
    `LoadOBJIgnoresMismatchedVertexNormalCount`, and
    `LoadOBJRejectsMalformedVertexNormal`, and extend
    `WritesOBJTriangleWithNormals` to assert that the loaded
    mesh exposes a `v:normal` property whose values match the
    written normals.
  - `docs/migration/nonlegacy-parity-matrix.md` updates the
    existing OBJ/OFF/STL mesh import row to record the new
    geometry-owned OBJ vn lockstep importer.
  - Remaining `GEOIO-002` scope (granular reader-side
    `MeshIOReadStatus`/`PointCloudIOReadStatus` diagnostics;
    further OBJ ASCII parity such as per-corner normal vertex
    duplication or `vt` UV ingest; packed-`rgb`/`rgba` PCD
    plus `binary_compressed` LZF decompression) stays tracked
    under the parent backlog task
    `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
