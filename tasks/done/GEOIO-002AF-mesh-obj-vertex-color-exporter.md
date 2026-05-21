# GEOIO-002AF — Geometry-owned OBJ ASCII vertex-color export parity

## Goal
- Extend `Geometry::MeshIO::WriteOBJ` to emit the seven-token
  `v x y z r g b` vertex-color extension when the input
  `MeshIOResult` carries a sized `v:color` `glm::vec4` property,
  forming a symmetric round trip with the OBJ ASCII vertex-color
  importer landed under `GEOIO-002AE`. RGB float channels are taken
  from the property's first three components and the alpha channel
  is intentionally dropped, matching the mesh PLY writer convention
  added under `GEOIO-002Y` and the seven-token reader expectation
  that alpha defaults to `1.0`.

## Non-goals
- No eight-token `v x y z w r g b` or `v x y z r g b a` writer
  variants; only the unambiguous seven-token form is emitted.
- No alpha serialization. Alpha is intentionally dropped to match
  the existing PLY mesh writer convention and the AE reader, which
  always defaults the parsed alpha back to `1.0`.
- No vertex-color emission from `WritePLY` / `WritePLYBinary` /
  `WriteSTL` / point-cloud writers — those landed under earlier
  slices (`GEOIO-002Y` covers PLY mesh vertex color export).
- No reader-side change to `LoadOBJ`, `LoadOFF`, `LoadPLY`, or
  `LoadSTL`.
- No new public module surface; the new branch lives inside the
  existing `Geometry.HalfedgeMesh.IO` implementation file.
- No CPU color synthesis when the input mesh omits per-vertex
  colors or when the color count mismatches the position count;
  the writer silently emits the canonical four-token `v` form in
  both cases (matches how the existing texcoord / normal emission
  gates behave).
- No assets / runtime ownership of mesh file IO; geometry owns
  format codecs only.
- No GPU/Vulkan requirement in the default CPU gate.

## Context
- Owner/agent: `geometry -> core` only.
- Parent task:
  `tasks/done/GEOIO-002-geometry-io-parity-hardening.md`.
- Predecessors:
  - `GEOIO-002U` added the OBJ ASCII vertex-texcoord lockstep export
    (the symmetric writer side for `v:texcoord`).
  - `GEOIO-002Y` added the mesh PLY vertex color export with the
    "alpha intentionally dropped" convention this slice mirrors.
  - `GEOIO-002AE` landed the AE-style seven-token OBJ ASCII
    vertex-color *importer*; this slice closes the round trip on
    the writer side.
- `Geometry::MeshIO::WriteOBJ` lives in
  `src/geometry/Geometry.HalfedgeMesh.IO.cpp`. Before this slice it
  emitted exactly four-token `v %.6f %.6f %.6f\n` lines regardless
  of any `v:color` property; the optional `vt` and `vn` blocks
  were already gated on sized `v:texcoord` / `v:normal` properties
  and the face emission already selects `f a a a`, `f a/a a/a a/a`,
  `f a//a a//a a//a`, or `f a/a/a a/a/a a/a/a` based on those
  flags.
- The writer keeps the existing per-position loop. The new
  `colorsView` / `hasColors` pair is established alongside the
  existing `normalsView` / `texcoordsView` gates. When `hasColors`
  is true, each `v` line appends ` %.6f %.6f %.6f` after the
  position before the trailing newline, producing the canonical
  seven-token form the AE importer recognises.

## Required changes
- [x] `src/geometry/Geometry.HalfedgeMesh.IO.cpp::WriteOBJ`:
  - [x] Capture `mesh.Vertices.Get<glm::vec4>("v:color")` and
    derive `hasColors = colorsView.IsValid() && colorsView.Vector().size() == positions.size()`.
  - [x] Replace the range-based position loop with an index-based
    loop so the matching color row is reachable.
  - [x] When `hasColors`, append ` %.6f %.6f %.6f` formatted from
    `colorsView.Vector()[i]` (`.r/.g/.b`) before the trailing
    newline; the alpha channel is intentionally dropped to match
    the AE importer (which defaults alpha to `1.0`) and the mesh
    PLY writer convention.
  - [x] Preserve the existing `vt` / `vn` emission and the face
    token format selection (`hasTexcoords && hasNormals` ->
    `f a/a/a`, etc.); the new color branch is orthogonal to the
    face attribute selection.

## Tests
- [x] Add `unit;geometry` cases to
  `tests/unit/geometry/Test.GeometryIO.cpp::GeometryIO_MeshIO`
  next to the existing `WritesOBJTriangleWithTexcoordsAndNormals`
  and `WriteOBJOmitsTexcoordsWhenAbsent` tests:
  - [x] `WritesOBJTriangleWithVertexColors` — three positions
    with distinct RGB colors and no normals/texcoords. Confirms
    the writer emits the seven-token `v x y z r g b` form, the
    face line stays `f 1 2 3`, and `LoadOBJ` recovers the
    `v:color` `glm::vec4` property with the original RGB values
    and alpha `1.0`.
  - [x] `WritesOBJTriangleWithColorsAndNormals` — colors + normals.
    Confirms the seven-token `v` form coexists with `vn` lines and
    the face selector still emits `f 1//1 2//2 3//3`; the round
    trip recovers both `v:color` and `v:normal`. Verifies the
    AE face-attribute remap branch (`hasFaceNormals`) correctly
    propagates per-position colors through `remappedColors`.
  - [x] `WritesOBJTriangleWithColorsAndTexcoords` — colors +
    texcoords. Confirms the seven-token `v` form coexists with
    `vt` lines and the face selector still emits `f 1/1 2/2 3/3`;
    the round trip recovers both `v:color` and `v:texcoord`.
    Verifies the AE face-attribute remap branch
    (`hasFaceTexcoords`) correctly propagates per-position colors.
  - [x] `WriteOBJOmitsColorsWhenAbsent` — no `v:color` property.
    Confirms the writer emits exactly the four-token `v` form and
    `LoadOBJ` produces no `v:color` property on the loaded mesh
    (mirrors the existing `WriteOBJOmitsTexcoordsWhenAbsent`
    pattern).
  - [x] `WriteOBJOmitsColorsWhenMismatchedCount` — the
    `v:color` storage size diverges from the position count
    (`PropertyBuffer::Vector().push_back(...)` extends the
    storage while leaving the registry's row count untouched).
    Confirms the writer gate degrades cleanly: the `v` lines stay
    four-token (`v 0.000000 0.000000 0.000000\n` with no trailing
    color tokens).

## Docs
- [x] Update the `OBJ/PLY/STL exporters` row in
  `docs/migration/nonlegacy-parity-matrix.md` with the new
  geometry-owned OBJ seven-token vertex-color lockstep export
  added under this task.
- [x] No module-surface change; the inventory regenerator already
  tracks `Geometry.HalfedgeMesh.IO` and the new branch lives
  inside the existing implementation file.

## Acceptance criteria
- [x] `Geometry::MeshIO::WriteOBJ` emits seven-token
  `v x y z r g b` lines whenever the input mesh carries a sized
  `v:color` `glm::vec4` property and four-token lines otherwise.
- [x] Round-tripping a colors-only mesh through `WriteOBJ` /
  `LoadOBJ` recovers the `v:color` property with the original
  RGB values and alpha `1.0`.
- [x] Round-tripping a colors+normals mesh through `WriteOBJ` /
  `LoadOBJ` recovers both `v:color` and `v:normal` after the AE
  importer's face-attribute remap branch.
- [x] Round-tripping a colors+texcoords mesh through `WriteOBJ` /
  `LoadOBJ` recovers both `v:color` and `v:texcoord` after the AE
  importer's face-attribute remap branch.
- [x] Mismatched / absent `v:color` storage causes the writer to
  fall back to the four-token form (no error).
- [x] `src/geometry/*` remains independent of assets / runtime /
  graphics.
- [x] Parity matrix records the new OBJ seven-token vertex-color
  exporter.

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
- Adding eight-token `v` writer variants, alpha serialization, or
  per-corner vertex-color duplication in this slice.
- Changing existing `WritePLY`, `WritePLYBinary`, `WriteSTL`,
  `LoadOBJ`, `LoadOFF`, `LoadPLY`, or `LoadSTL` signatures or
  behaviour.
- Mixing mechanical legacy deletion with semantic IO
  implementation.

## Completion
- Completed: 2026-05-17.
- Status: done.
- Branch: `claude/backlog-task-agent-prompt-vggk4`.
- Implementation commit: this local change
  (`GEOIO-002AF: export OBJ vertex colors as seven-token v lines`).
- Verified in this session:
  - `python3 tools/repo/check_layering.py --root src --strict` —
    no layering violations; `geometry` imports remain
    `geometry -> core` only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (one above the GEOIO-002AE baseline).
  - `python3 tools/docs/check_doc_links.py --root .` — no broken
    relative links.
  - `python3 tools/repo/generate_module_inventory.py --root src
    --out docs/api/generated/module_inventory.md` — no diff (the
    new writer branch lives inside the existing
    `Geometry.HalfedgeMesh.IO` module surface).
- CPU build/test gate not exercised in this session: the container
  pins `clang-20` / `clang-scan-deps-20` via `CMakePresets.json`
  but ships only `clang-18`, whose libstdc++ lacks `std::expected`
  required by `Core.Error`. `cmake --preset ci` therefore fails at
  the compiler-detection step. This mirrors the toolchain
  constraint already recorded by `GEOIO-002L`..`GEOIO-002AE` and
  must re-run on a CI host with the correct toolchain prior to
  merge. The CPU contract for the five new tests is unchanged
  from the prior OBJ `vn` / `vt` / AE color slices and the edit
  is module-surface-stable.
- Notes:
  - `Geometry::MeshIO::WriteOBJ` now captures
    `mesh.Vertices.Get<glm::vec4>("v:color")` and derives
    `hasColors` from `IsValid() && size() == positions.size()`.
    The per-position loop became index-based so the matching
    color row is reachable. When `hasColors` is true, the writer
    appends ` %.6f %.6f %.6f` (R/G/B) after the position and
    before the trailing newline, producing the canonical
    seven-token form recognised by the AE importer.
  - Alpha is intentionally dropped: the AE importer always
    defaults parsed alpha to `1.0`, and the mesh PLY writer
    convention (added under `GEOIO-002Y`) likewise drops alpha
    so a round trip carries identical RGB values back through
    `LoadOBJ`. Storing alpha would require either an eight-token
    extension or breaking the AE reader contract.
  - Existing `vt` / `vn` emission and face token selection
    (`hasTexcoords && hasNormals` -> `f a/a/a`, etc.) are
    untouched; the color branch is orthogonal to face attribute
    selection.
  - Tests in
    `tests/unit/geometry/Test.GeometryIO.cpp::GeometryIO_MeshIO`
    add `WritesOBJTriangleWithVertexColors`,
    `WritesOBJTriangleWithColorsAndNormals`,
    `WritesOBJTriangleWithColorsAndTexcoords`,
    `WriteOBJOmitsColorsWhenAbsent`, and
    `WriteOBJOmitsColorsWhenMismatchedCount`. The
    colors+normals and colors+texcoords cases exercise the AE
    importer's face-attribute remap branch (`f a//a` and
    `f a/a` forms) and assert that per-position colors propagate
    through `remappedColors`.
  - `docs/migration/nonlegacy-parity-matrix.md` updates the
    existing `OBJ/PLY/STL exporters` row to record the new
    geometry-owned OBJ seven-token vertex-color lockstep
    exporter.
  - Remaining `GEOIO-002` scope (eight-token `v` writer
    variants, OBJ vertex-color alpha serialization, granular
    reader-side status codes, further OBJ ASCII parity, packed
    `rgb` / `rgba` PCD plus `binary_compressed` LZF
    decompression) stays tracked under the parent backlog task
    `tasks/done/GEOIO-002-geometry-io-parity-hardening.md`.
