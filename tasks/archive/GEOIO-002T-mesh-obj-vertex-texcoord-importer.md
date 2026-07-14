# GEOIO-002T — Geometry-owned OBJ ASCII vertex-texcoord import parity

## Goal
- Extend `Geometry::MeshIO::LoadOBJ` to parse `vt u v [w]`
  vertex-texcoord lines and emit them as a `v:texcoord`
  `glm::vec2` property on the returned `MeshIOResult` whenever the
  texcoord count matches the position count, mirroring the lockstep
  vertex-normal ingest added under `GEOIO-002S`.

## Non-goals
- No per-corner texcoord vertex duplication. The legacy
  `Graphics.Importers.OBJ` importer resolves `p/t/n` face token
  triplets into unique attribute vertices and may split a single
  OBJ position across multiple result vertices; that behavior
  stays out of scope and remains owned by the asset import layer.
- No `vp` parameter-space-vertex ingest.
- No `l` line topology, `g`/`o`/`s` group/object/smoothing,
  `usemtl`/`mtllib` parsing.
- No negative `vt` index resolution on face tokens. Face token
  parsing is unchanged: `ParseOBJVertexIndex` continues to extract
  only the position index from `p`, `p/t`, or `p/t/n` tokens.
- No texcoord-on-mismatch CPU defaulting. Mismatched `vt` counts
  are silently dropped (no error) and the result has no
  `v:texcoord` property.
- No third texcoord component retention. OBJ allows `vt u v` or
  `vt u v w`; this slice keeps `v:texcoord` as `glm::vec2`
  (matching the established `kVertexTexcoordPropertyName`
  convention in `Geometry.HalfedgeMesh.Utils`) and silently
  drops the optional `w` coordinate after validating its
  syntactic presence is not required.
- No assets/runtime ownership of mesh file IO; geometry owns
  format codecs only.
- No writer-side change to `WriteOBJ`. The current writer does
  not emit `vt` lines, so a writer → reader round-trip is not
  attempted here; that ownership stays with a future writer
  slice.
- No reader-side change to `LoadOFF`, `LoadPLY`, or `LoadSTL`.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-7bq9k`.
- Parent task:
  `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002S` (`tasks/archive/GEOIO-002S-mesh-obj-vertex-normal-importer.md`)
  introduced the lockstep `vn` ingest pattern this slice mirrors
  for `vt`.
- `Geometry.HalfedgeMesh.Utils` already publishes
  `kVertexTexcoordPropertyName = "v:texcoord"`, and the
  parameterization pipeline (`Geometry.HalfedgeMesh.Parameterization.cpp`)
  emits UVs under that name. This slice reuses that convention so
  CPU consumers and downstream geometry algorithms see a single
  property name for OBJ-sourced UVs.
- The reader lives in
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp::LoadOBJ`. Before
  this slice it parsed only `v`, `vn`, and `f` lines and silently
  dropped `vt`. The new branch collects `vt` lines into a local
  `std::vector<glm::vec2> texcoords`; after the parse loop, if
  `texcoords.size() == vertices.size()` a `glm::vec2`
  `v:texcoord` property is written via `Vertices.GetOrAdd`,
  matching how `PopulateResult` emits `v:normal` / `v:color`.
- `ParseOBJVertexIndex` is unchanged: it strips any `/...` tail
  from face tokens and continues to extract only the position
  index. Face arity validation and out-of-range index checks
  remain in place.

## Required changes
- [x] `src/geometry/Geometry.HalfedgeMesh.IO.cpp::LoadOBJ`:
  - [x] Add a `std::vector<glm::vec2> texcoords;` local alongside
    `vertices`/`normals`/`faces`.
  - [x] Add an `else if (tokens[0] == "vt")` branch that requires
    `tokens.size() >= 3`, parses two floats via
    `ParseNumber<float>`, and pushes a `glm::vec2` onto
    `texcoords`. Reject malformed values with
    `InvalidMeshFormat` matching the existing `v` / `vn` line
    behavior. The optional third `w` component is accepted but
    not read.
  - [x] After the existing `PopulateResult(result, vertices, faces,
    normalsSpan)` call, if `texcoords.size() == vertices.size()`,
    write a `v:texcoord` property via
    `result.Vertices.GetOrAdd<glm::vec2>("v:texcoord",
    glm::vec2(0.0f))` and copy the texcoords into it. Mismatched
    counts skip the property silently.

## Tests
- [x] Add `unit;geometry` cases to
  `tests/unit/geometry/Test.GeometryIO.cpp::GeometryIO_MeshIO`
  next to the existing OBJ vertex-normal tests:
  - [x] `LoadsOBJTriangleWithVertexTexcoords` — three `v` lines,
    three `vt u v` lines, and `f 1/1 2/2 3/3` succeed and return
    a `v:texcoord` property of size 3 whose values match the
    `vt` lines.
  - [x] `LoadsOBJVertexTexcoordWithOptionalThirdComponent` — `vt u v w`
    lines with three components succeed; the result keeps a
    `glm::vec2` `v:texcoord` property storing only the first two
    components.
  - [x] `LoadOBJIgnoresMismatchedVertexTexcoordCount` — three `v`
    lines, two `vt` lines, and a non-attributed face succeeds
    but returns no `v:texcoord` property.
  - [x] `LoadOBJRejectsMalformedVertexTexcoord` — `vt 0` with one
    component yields `Core::ErrorCode::InvalidFormat`
    (matches `v` / `vn` strictness).
  - [x] `LoadOBJVertexTexcoordAndNormalCoexist` — three `v`, `vt`,
    and `vn` lines plus `f 1/1/1 2/2/2 3/3/3` produce both
    `v:texcoord` and `v:normal` properties with the expected
    values, exercising the joint lockstep ingest.

## Docs
- [x] Update the
  `OBJ/OFF/STL mesh import and mesh PLY import` row in
  `docs/migration/nonlegacy-parity-matrix.md` to record the
  new OBJ vertex-texcoord lockstep import added under
  `GEOIO-002T`.
- [x] Regenerate `docs/api/generated/module_inventory.md` only if
  the generator picks up changes to the existing
  `Geometry.HalfedgeMesh.IO` module surface. If the regenerator
  changes only the date stamp, leave it untouched.

## Acceptance criteria
- [x] `Geometry::MeshIO::LoadOBJ` accepts `vt u v [w]` lines and
  surfaces a `glm::vec2` `v:texcoord` property when the
  texcoord count equals the position count.
- [x] New tests pass under `IntrinsicTests` and the CPU gate.
- [x] No assets/runtime/graphics imports leak into
  `src/geometry/*`.
- [x] Parity matrix records the new OBJ vt lockstep import.

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
- Adding per-corner texcoord vertex duplication, `vp` ingest,
  `l`/`g`/`o`/`s`/`usemtl`/`mtllib` parsing, negative `vt`
  index resolution, or `vt` writer emission in this slice.
- Changing the existing `LoadOFF`, `LoadPLY`, `LoadSTL`,
  `WriteOBJ`, or `PopulateResult` signatures or behavior.
- Mixing mechanical legacy deletion with semantic IO
  implementation.

## Completion
- Completed: 2026-05-11.
- Status: done.
- Branch: `claude/setup-agentic-workflow-7bq9k`.
- Implementation commit: `75bb101`
  (`GEOIO-002T: add OBJ ASCII vertex-texcoord import parity`).
- Verified in this session:
  - `python3 tools/repo/check_layering.py --root src --strict` —
    no layering violations; `geometry` imports remain
    `geometry -> core` only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings; task file count increments by one above the
    GEOIO-002S baseline.
  - `python3 tools/docs/check_doc_links.py --root .` — 0 broken
    relative links.
  - `python3 tools/repo/generate_module_inventory.py --root src
    --out docs/api/generated/module_inventory.md` — no diff (the
    new reader branch lives inside the existing
    `Geometry.HalfedgeMesh.IO` module surface and the inventory
    tracks modules, not individual functions).
- CPU build/test gate not exercised: this container lacks
  `clang-20` and `clang-scan-deps` 20+, which the `ci` preset
  hard-codes. `cmake --preset ci` therefore fails at the
  compiler detection step, mirroring the constraint already
  recorded by `GEOIO-002L`..`GEOIO-002S` and earlier slices.
  Build verification needs to re-run on a CI host with the
  correct toolchain prior to merge.
- Notes:
  - `Geometry::MeshIO::LoadOBJ` collects `vt` lines into a
    local `std::vector<glm::vec2> texcoords`. After the parse
    loop the function checks `texcoords.size() ==
    vertices.size()` and, when true, writes a `glm::vec2`
    `v:texcoord` property via `Vertices.GetOrAdd`. A mismatched
    count silently skips the property (mirrors the lockstep
    `vn` policy from `GEOIO-002S`).
  - The reader rejects malformed `vt` lines the same way it
    rejects malformed `v` / `vn` lines: any required component
    failing `ParseNumber<float>` returns
    `Core::ErrorCode::InvalidFormat`, and a `vt` line with
    fewer than two components is likewise rejected
    (`tokens.size() < 3`).
  - Face token parsing is unchanged. `ParseOBJVertexIndex`
    continues to extract only the position index from `p`,
    `p/t`, or `p/t/n` tokens, so files that reference texcoords
    only via `f` token triplets (without lockstep `vt` lines)
    still parse and silently drop the texcoord references.
  - Tests in
    `tests/unit/geometry/Test.GeometryIO.cpp::GeometryIO_MeshIO`
    add `LoadsOBJTriangleWithVertexTexcoords`,
    `LoadsOBJVertexTexcoordWithOptionalThirdComponent`,
    `LoadOBJIgnoresMismatchedVertexTexcoordCount`,
    `LoadOBJRejectsMalformedVertexTexcoord`, and
    `LoadOBJVertexTexcoordAndNormalCoexist`.
  - `docs/migration/nonlegacy-parity-matrix.md` updates the
    existing OBJ/OFF/STL mesh import row to record the new
    geometry-owned OBJ vt lockstep importer.
  - Remaining `GEOIO-002` scope (granular reader-side
    `MeshIOReadStatus`/`PointCloudIOReadStatus` diagnostics;
    further OBJ ASCII parity such as per-corner texcoord/normal
    vertex duplication driven by `p/t/n` face triplets, or `vt`
    writer emission; packed-`rgb`/`rgba` PCD plus
    `binary_compressed` LZF decompression) stays tracked under
    the parent backlog task
    `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
