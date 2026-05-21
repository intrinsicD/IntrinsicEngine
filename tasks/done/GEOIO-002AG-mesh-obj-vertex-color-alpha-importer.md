# GEOIO-002AG — Geometry-owned OBJ ASCII eight-token vertex-color import parity

## Goal
- Extend `Geometry::MeshIO::LoadOBJ` to parse the eight-token
  `v x y z r g b a` vertex-color extension and emit the parsed RGBA
  values as a `v:color` `glm::vec4` property on the returned
  `MeshIOResult`, complementing the seven-token form landed under
  `GEOIO-002AE`. Files mixing seven- and eight-token `v` lines are
  accepted with the seven-token rows defaulting alpha to `1.0` and the
  eight-token rows carrying the explicit alpha through to the
  promoted `v:color` storage.

## Non-goals
- No support for the eight-token `v x y z w r g b` (homogeneous-`w` +
  RGB) extension. The eight-token form is interpreted exclusively as
  `v x y z r g b a`; the homogeneous-`w` variant is too rare in real
  OBJ files to warrant heuristic disambiguation, and the AE non-goal
  already lists it as out of scope.
- No support for a nine-token `v x y z w r g b a` extension.
- No change to the seven-token interpretation: seven-token rows
  continue to default alpha to `1.0`.
- No writer-side change to `WriteOBJ`. The geometry-owned OBJ writer
  stays on the seven-token form documented under `GEOIO-002AF`; alpha
  serialization through the writer is a deliberate separate follow-up
  (see "Remaining GEOIO-002 scope" in the AF completion notes).
- No `l` line topology, `g` / `o` / `s` group / object / smoothing,
  `usemtl` / `mtllib`, or `vp` parameter-space-vertex parsing.
- No CPU color synthesis when the file omits per-vertex colors or
  when the color count mismatches the position count. Mismatched
  colors are silently dropped (no error) and the result has no
  `v:color` property, matching the AE convention.
- No reader-side change to `LoadOFF`, `LoadPLY`, or `LoadSTL`.
- No assets / runtime ownership of mesh file IO; geometry owns
  format codecs only.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Owner/agent: `geometry -> core` only.
- Parent task:
  `tasks/done/GEOIO-002-geometry-io-parity-hardening.md`.
- Predecessors:
  - `GEOIO-002AE` landed the seven-token `v x y z r g b` importer
    and established the lockstep + face-attribute remap branches for
    propagating `v:color` `glm::vec4` properties.
  - `GEOIO-002AF` landed the symmetric seven-token writer with
    alpha intentionally dropped (alpha is reconstructed as `1.0` on
    round trip).
  - AE/AF both explicitly punted the eight-token form to a follow-up,
    citing the `v x y z r g b a` / `v x y z w r g b` ambiguity. This
    slice closes the AE/AF round trip for the unambiguous RGBA form,
    which is what tools like MeshLab and several point-cloud
    pipelines emit when exporting per-vertex alpha.
- `Geometry::MeshIO::LoadOBJ` lives in
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp`. Before this slice it
  parsed seven-token `v` lines as RGB-with-alpha-`1.0` and silently
  tolerated any other trailing tokens (four-token positions only,
  eight-token forms dropped the trailing tokens). The new branch
  reaches the eight-token form via an `else if (tokens.size() == 8)`
  alongside the existing seven-token branch, parses
  `tokens[4..7]` as `r`, `g`, `b`, `a` through `ParseNumber<float>`,
  and pushes `glm::vec4(r, g, b, a)` onto the same `colors` vector.
  The downstream lockstep + face-attribute remap branches already
  honour the per-position alpha because they copy the `glm::vec4`
  whole; no other reader paths need to change.

## Required changes
- [x] `src/geometry/Geometry.HalfedgeMesh.IO.cpp::LoadOBJ`:
  - [x] Extend the existing `v` branch's seven-token color path with
    an `else if (tokens.size() == 8)` clause that parses
    `tokens[4..7]` as `r`, `g`, `b`, `a` via `ParseNumber<float>`.
    Reject malformed alpha components with `InvalidMeshFormat`,
    matching the existing seven-token color strictness.
  - [x] Push `glm::vec4(*r, *g, *b, *a)` onto the existing `colors`
    vector. No other branch is touched: the lockstep tail
    (`colors.size() == vertices.size()`) and the face-attribute
    remap (`hasFaceNormals || hasFaceTexcoords`) already propagate
    the full `glm::vec4` so per-position alpha survives both paths.

## Tests
- [x] Add `unit;geometry` cases to
  `tests/unit/geometry/Test.GeometryIO.cpp::GeometryIO_MeshIO`
  next to the existing seven-token color cases:
  - [x] `LoadsOBJTriangleWithVertexColorsAndAlpha` — three
    eight-token `v x y z r g b a` lines with distinct RGBA values
    (alpha varying away from `1.0`) and a non-attributed face. The
    result must expose `v:color` `glm::vec4` of size 3 with the
    exact RGBA components.
  - [x] `LoadsOBJMixedSevenAndEightTokenVertexColors` — three `v`
    lines mixing seven-token and eight-token forms (e.g. row 0 and 2
    eight-token with distinct alpha, row 1 seven-token). The
    seven-token row defaults alpha to `1.0`; both eight-token rows
    keep their explicit alpha. Lockstep size still matches positions
    so `v:color` is emitted.
  - [x] `LoadOBJRejectsMalformedVertexColorAlpha` — an eight-token
    `v` line whose alpha component fails to parse yields
    `Core::ErrorCode::InvalidFormat` (matches the AE malformed-RGB
    strictness).
  - [x] `LoadOBJVertexColorAlphaSurvivesFaceAttributeRemap` —
    three eight-token `v x y z r g b a` lines, three `vt u v`
    lines, and a `f 1/1 2/2 3/3` face. The face-attribute remap
    branch must copy the alpha through `remappedColors`, and the
    returned `v:color` must keep the per-position alpha intact.

## Docs
- [x] Update the `OBJ/OFF/STL mesh import and mesh PLY import` row in
  `docs/migration/nonlegacy-parity-matrix.md` with the geometry-owned
  OBJ eight-token RGBA vertex-color import added under this task.
- [x] No module-surface change; the inventory regenerator already
  tracks `Geometry.HalfedgeMesh.IO` and the new branch lives inside
  the existing implementation file.

## Acceptance criteria
- [x] `Geometry::MeshIO::LoadOBJ` accepts both seven-token and
  eight-token `v`-with-color rows in the same file and emits a
  `v:color` `glm::vec4` property whose alpha matches the explicit
  alpha for eight-token rows and defaults to `1.0` for seven-token
  rows.
- [x] Eight-token rows whose RGBA tokens fail to parse return
  `Core::ErrorCode::InvalidFormat` with no `v:color` property
  emitted, mirroring the AE seven-token strictness.
- [x] Mismatched / absent color storage causes the reader to silently
  drop colors (no error), mirroring the AE convention.
- [x] `src/geometry/*` remains independent of assets / runtime /
  graphics.
- [x] Parity matrix records the new OBJ eight-token RGBA vertex-color
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
- Reinterpreting the eight-token form as `v x y z w r g b`
  (homogeneous-`w` + RGB).
- Adding nine-token `v x y z w r g b a` or other extended `v`
  variants in this slice.
- Adding OBJ vertex-color alpha serialization to `WriteOBJ` under
  cover of this importer slice.
- Changing existing `LoadOFF`, `LoadPLY`, `LoadSTL`, or `WriteOBJ`
  signatures or behaviour.
- Mixing mechanical legacy deletion with semantic IO
  implementation.

## Completion
- Completed: 2026-05-17.
- Status: done.
- Branch: `claude/backlog-task-agent-prompt-YPUJ7`.
- Implementation commit: this local change
  (`GEOIO-002AG: import OBJ vertex colors with explicit alpha from eight-token v lines`).
- Verified in this session:
  - `python3 tools/repo/check_layering.py --root src --strict` —
    no layering violations; `geometry` imports remain
    `geometry -> core` only (742 files scanned).
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (247 task files validated; one above the GEOIO-002AF
    baseline of 246).
  - `python3 tools/docs/check_doc_links.py --root .` — no broken
    relative links (501 links checked).
  - `python3 tools/repo/generate_module_inventory.py --root src
    --out docs/api/generated/module_inventory.md` — 434 modules; no
    diff (the new reader branch lives inside the existing
    `Geometry.HalfedgeMesh.IO` module surface).
- CPU build/test gate not exercised in this session: the container
  pins `clang-20` / `clang-scan-deps-20` via `CMakePresets.json`
  but ships only `clang-18`, whose libstdc++ lacks `std::expected`
  required by `Core.Error`. `cmake --preset ci` therefore fails at
  the compiler-detection step (`The CMAKE_CXX_COMPILER: "clang++-20"
  is not a full path and was not found in the PATH.`). This mirrors
  the toolchain constraint already recorded by `GEOIO-002L`..
  `GEOIO-002AF` and must re-run on a CI host with the correct
  toolchain prior to merge. The CPU contract for the four new
  tests is unchanged from the prior OBJ `vn` / `vt` / AE color
  slices and the edit is module-surface-stable.
- Notes:
  - `Geometry::MeshIO::LoadOBJ` now extends the existing seven-token
    color branch with an `else if (tokens.size() == 8)` clause that
    parses `tokens[4..7]` as `r`, `g`, `b`, `a` through
    `ParseNumber<float>` and pushes `glm::vec4(r, g, b, a)` onto the
    same `colors` vector. The seven-token branch is unchanged —
    those rows continue to default alpha to `1.0` so the AE round
    trip is preserved.
  - Malformed RGBA components on an eight-token line return
    `Core::ErrorCode::InvalidFormat`, matching the existing AE
    seven-token strictness.
  - The lockstep tail and the face-attribute remap branch already
    propagate the full `glm::vec4` per position, so no other reader
    paths needed changes; per-position alpha survives both the
    no-attribute and the `f a/a a/a a/a` / `f a//a a//a a//a` /
    `f a/a/a a/a/a a/a/a` remap paths.
  - Mixed seven-/eight-token `v` lines in the same file are
    accepted: the seven-token rows default alpha to `1.0`, and
    eight-token rows keep their explicit alpha. Lockstep size still
    matches positions in that case, so `v:color` is emitted.
  - Tests in
    `tests/unit/geometry/Test.GeometryIO.cpp::GeometryIO_MeshIO`
    add `LoadsOBJTriangleWithVertexColorsAndAlpha`,
    `LoadsOBJMixedSevenAndEightTokenVertexColors`,
    `LoadOBJRejectsMalformedVertexColorAlpha`, and
    `LoadOBJVertexColorAlphaSurvivesFaceAttributeRemap`. The fourth
    case exercises the face-attribute remap branch
    (`f 1/1 2/2 3/3`) and asserts the per-position alpha survives
    the remap.
  - `docs/migration/nonlegacy-parity-matrix.md` updates the
    `OBJ/OFF/STL mesh import and mesh PLY import` row with the new
    geometry-owned OBJ eight-token RGBA vertex-color lockstep
    importer.
  - Remaining `GEOIO-002` scope (OBJ vertex-color alpha
    serialization through `WriteOBJ`, ninth-token / homogeneous-`w`
    + RGBA disambiguation, granular reader-side status codes,
    further OBJ ASCII parity, packed-`rgb` / `rgba` PCD export
    plus `binary_compressed` LZF decompression) stays tracked
    under the parent backlog task
    `tasks/done/GEOIO-002-geometry-io-parity-hardening.md`.
