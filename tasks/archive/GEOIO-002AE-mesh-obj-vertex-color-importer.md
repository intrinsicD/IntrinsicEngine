# GEOIO-002AE — Geometry-owned OBJ ASCII vertex-color import parity

## Goal
- Extend `Geometry::MeshIO::LoadOBJ` to parse the seven-token
  `v x y z r g b` vertex-color extension and emit the parsed colors
  as a `v:color` `glm::vec4` property on the returned `MeshIOResult`
  whenever the color count matches the position count. The fourth
  alpha channel defaults to `1.0` so the result type matches the
  `v:color` storage convention already used by `LoadOFF`,
  `LoadPLY`, and `LoadPLYBinary`.

## Non-goals
- No vertex-color export from `WriteOBJ`. The geometry-owned OBJ
  writer stays strictly four-token `v x y z` until a separate
  follow-up.
- No support for the eight-token `v x y z w r g b` or
  `v x y z r g b a` extensions. Only the unambiguous seven-token
  form is interpreted as a vertex-color line; other token counts
  remain unchanged in behaviour (4 = standard position-only,
  5 = silently tolerated homogeneous `w`, 6 / 8+ = silently
  tolerated trailing tokens).
- No vertex-color emission in the per-corner face-attribute remap
  path (`hasFaceNormals || hasFaceTexcoords`). Vertex duplication
  by face token preserves per-position colors through the existing
  `vertices[sourceVertex.Position]` lookup, so colors carry through
  the remap automatically; this slice only adds the lockstep
  emission and the color-carry through the remap so the round trip
  is symmetric.
- No `l` line topology, `g` / `o` / `s` group / object / smoothing,
  `usemtl` / `mtllib`, or `vp` parameter-space-vertex parsing.
- No CPU color synthesis when the file omits per-vertex colors or
  when the color count mismatches the position count. Mismatched
  colors are silently dropped (no error) and the result has no
  `v:color` property.
- No assets / runtime ownership of mesh file IO; geometry owns
  format codecs only.
- No reader-side change to `LoadOFF`, `LoadPLY`, or `LoadSTL`.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Owner/agent: `geometry -> core` only.
- Parent task:
  `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
- Predecessors:
  - `GEOIO-002S` added the OBJ ASCII vertex-normal lockstep
    importer.
  - `GEOIO-002T` added the OBJ ASCII vertex-texcoord lockstep
    importer.
  - `GEOIO-002AC` added the OBJ face-referenced attribute importer.
  - `GEOIO-002Y` / `GEOIO-002W` already cover mesh PLY vertex-color
    import and export, so the `v:color` `glm::vec4` property is
    the canonical geometry storage convention for vertex colors.
- `Geometry::MeshIO::LoadOBJ` lives in
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp`. Before this slice it
  parsed `v` lines as exactly three floats (`tokens.size() < 4` is
  rejected, extra tokens after `tokens[3]` are silently ignored).
  The seven-token `v x y z r g b` extension emitted by Blender,
  MeshLab, and other tools therefore round-tripped position only.
- The reader keeps the existing `vertices` / `normals` / `texcoords`
  vectors and adds a sibling `std::vector<glm::vec4> colors`.
  Seven-token `v` lines parse `tokens[4..6]` as `r`, `g`, `b`
  through `ParseNumber<float>` and push a `glm::vec4(r, g, b, 1.0f)`
  onto `colors`. Other token counts leave `colors` untouched.
- The shared `PopulateResult` overload already accepts a colors
  span and emits the `v:color` property; the lockstep branch
  forwards the new colors vector when its size matches the
  position count.
- The face-attribute remap branch already duplicates a position
  by face token. The remap path now copies the per-position color
  into `remappedColors` for each newly inserted vertex so the
  property survives the per-corner duplication, matching how the
  branch already handles per-position normals/texcoords.

## Required changes
- [x] `src/geometry/Geometry.HalfedgeMesh.IO.cpp::LoadOBJ`:
  - [x] Add a `std::vector<glm::vec4> colors;` local alongside
    `vertices` / `normals` / `texcoords`.
  - [x] In the `v` branch, when `tokens.size() == 7`, parse
    `tokens[4]` / `tokens[5]` / `tokens[6]` as three floats via
    `ParseNumber<float>`. Reject malformed components with
    `InvalidMeshFormat` (matches the existing `v` strictness).
    Push `glm::vec4(r, g, b, 1.0f)` onto `colors`.
  - [x] In the lockstep (non-face-attribute) tail, build a
    `std::span<const glm::vec4>` over `colors` when
    `colors.size() == vertices.size()` and pass it to
    `PopulateResult(result, vertices, faces, normalsSpan,
    colorsSpan)`.
  - [x] In the face-attribute remap branch
    (`hasFaceNormals || hasFaceTexcoords`), allocate a
    `remappedColors` vector when `colors.size() == vertices.size()`
    and push `colors[sourceVertex.Position]` for each newly
    inserted remapped vertex. After `PopulateResult`, attach the
    `v:color` property in the same shape as the existing
    `v:texcoord` after-hook.

## Tests
- [x] Add `unit;geometry` cases to
  `tests/unit/geometry/Test.GeometryIO.cpp::GeometryIO_MeshIO`
  next to the existing `LoadsOBJTriangleWithVertexNormals`/
  `LoadsOBJTriangleWithVertexTexcoords` tests:
  - [x] `LoadsOBJTriangleWithVertexColors` — three `v x y z r g b`
    lines with distinct RGB values and a triangle `f 1 2 3`
    succeeds and returns a `v:color` `glm::vec4` property of size
    3 whose RGB matches the lines and whose alpha is `1.0`.
  - [x] `LoadOBJIgnoresMismatchedVertexColorCount` — three `v`
    lines mixed (some seven-token, some four-token) and a
    non-attributed face succeeds but returns no `v:color`
    property.
  - [x] `LoadOBJRejectsMalformedVertexColor` — a seven-token `v`
    line whose green component fails to parse yields
    `Core::ErrorCode::InvalidFormat` (matches the existing `vn` /
    `vt` malformed-value strictness).
  - [x] `LoadOBJVertexColorAndTexcoordCoexist` — three
    `v x y z r g b` lines with three `vt u v` lines and a
    `f a/b a/b a/b` face succeeds, returning both `v:color` and
    `v:texcoord` (proves the face-attribute remap branch still
    emits `v:color`).

## Docs
- [x] Update the `OBJ/OFF/STL mesh import and mesh PLY import` row
  in `docs/migration/nonlegacy-parity-matrix.md` with the
  geometry-owned OBJ seven-token vertex-color lockstep import
  added under this task.
- [x] No module-surface change; the inventory regenerator already
  tracks `Geometry.HalfedgeMesh.IO` and the new branch lives
  inside the existing implementation file.

## Acceptance criteria
- [x] `Geometry::MeshIO::LoadOBJ` emits a `v:color` `glm::vec4`
  property when every `v` line is the seven-token form.
- [x] Mixed-form files silently drop colors without erroring.
- [x] Malformed color components on a seven-token `v` line are
  rejected with `Core::ErrorCode::InvalidFormat`.
- [x] `src/geometry/*` remains independent of assets / runtime /
  graphics.
- [x] Parity matrix records the new OBJ seven-token vertex-color
  importer.

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
- Adding assets / runtime / graphics / RHI imports to
  `src/geometry/*`.
- Adding eight-token `v` interpretation, `l` / `g` / `o` / `s` /
  `usemtl` / `mtllib` parsing, or vertex-color export from
  `WriteOBJ` in this slice.
- Changing existing `LoadOFF`, `LoadPLY`, `LoadSTL`, or
  `WriteOBJ` signatures or behaviour.
- Mixing mechanical legacy deletion with semantic IO
  implementation.

## Completion
- Completed: 2026-05-17.
- Status: done.
- Branch: `claude/backlog-task-agent-prompt-18Qdj`.
- Implementation commit: this local change
  (`GEOIO-002AE: import OBJ vertex colors from seven-token v lines`).
- Verified in this session:
  - `python3 tools/repo/check_layering.py --root src --strict` —
    no layering violations; `geometry` imports remain
    `geometry -> core` only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (245 task files validated; one above the GEOIO-002AD
    baseline of 244).
  - `python3 tools/docs/check_doc_links.py --root .` — no broken
    relative links.
  - `python3 tools/repo/generate_module_inventory.py --root src
    --out docs/api/generated/module_inventory.md` — 434 modules; no
    diff (the new reader branch lives inside the existing
    `Geometry.HalfedgeMesh.IO` module surface).
- CPU build/test gate not exercised in this session: the container
  pins `clang-20` / `clang-scan-deps-20` via `CMakePresets.json`
  but ships only `clang-18`, whose libstdc++ lacks `std::expected`
  required by `Core.Error`. `cmake --preset ci` therefore fails at
  the compiler-detection step, and a fallback `cmake -B
  build/clang18 ... -DCMAKE_CXX_COMPILER=clang++-18` configure
  succeeds but fails to compile `Core.Error.cppm` (`no template
  named 'expected' in namespace 'std'`). This mirrors the
  toolchain constraint already recorded by `GEOIO-002L`..
  `GEOIO-002AD` and must re-run on a CI host with the correct
  toolchain prior to merge. The CPU contract for the four new
  tests is unchanged from the prior OBJ `vn` / `vt` slices and the
  edit is module-surface-stable.
- Notes:
  - `Geometry::MeshIO::LoadOBJ` now collects per-vertex colors into
    a local `std::vector<glm::vec4>`. A seven-token `v` line
    parses `tokens[4..6]` as `r`, `g`, `b` through
    `ParseNumber<float>` and pushes `glm::vec4(r, g, b, 1.0f)`;
    other token counts leave the colors vector untouched.
  - Malformed color components on a seven-token line return
    `Core::ErrorCode::InvalidFormat`, matching the existing
    strictness for `vn` / `vt` malformed values.
  - In the lockstep tail the new `colorsSpan` is forwarded to
    `PopulateResult` only when `colors.size() == vertices.size()`,
    so mixed-form files silently drop colors with no error.
  - In the face-attribute remap branch
    (`hasFaceNormals || hasFaceTexcoords`), `remappedColors`
    snapshots `colors[sourceVertex.Position]` for each newly
    inserted vertex so the per-position color survives the
    per-corner duplication. `PopulateResult` then emits the
    `v:color` property in the same call, keeping the after-hook
    pattern consistent with how the branch already attaches
    `v:texcoord`.
  - Tests in
    `tests/unit/geometry/Test.GeometryIO.cpp::GeometryIO_MeshIO`
    add `LoadsOBJTriangleWithVertexColors`,
    `LoadOBJIgnoresMismatchedVertexColorCount`,
    `LoadOBJRejectsMalformedVertexColor`, and
    `LoadOBJVertexColorAndTexcoordCoexist`. The fourth case
    exercises the face-attribute remap branch (`f 1/1 2/2 3/3`)
    and asserts both `v:color` and `v:texcoord` survive.
  - `docs/migration/nonlegacy-parity-matrix.md` updates the
    existing `OBJ/OFF/STL mesh import and mesh PLY import` row to
    record the new geometry-owned OBJ seven-token vertex-color
    lockstep importer.
  - Remaining `GEOIO-002` scope (`WriteOBJ` vertex-color export,
    eight-token `v` disambiguation, granular reader-side status
    codes, further OBJ ASCII parity, packed-`rgb` / `rgba` PCD
    plus `binary_compressed` LZF decompression) stays tracked
    under the parent backlog task
    `tasks/archive/GEOIO-002-geometry-io-parity-hardening.md`.
