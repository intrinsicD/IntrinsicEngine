# GEOIO-002N — Geometry-owned text TGF graph exporter

## Goal
- Add a geometry-owned text TGF (Trivial Graph Format) exporter API to
  `Geometry.Graph.IO` that serializes a `GraphIOResult`
  (mandatory vertex positions; optional `v:label`, `e:weight`,
  `e:label` properties) without introducing assets/runtime/graphics
  dependencies, so the broader `GEOIO-002` parity work can grow graph
  round-trip coverage symmetric to the existing TGF importer
  (`Geometry::GraphIO::LoadTGF`).

## Non-goals
- No edge-list (`.edges`) or other graph-format exporter in this
  slice; TGF is the format the existing reader covers most fully and
  is the minimal addition needed for round-trip parity.
- No assets/runtime ownership of graph file IO; geometry owns
  format codecs only.
- No new format-detection metadata helpers.
- No legacy graphics graph exporter retirement (none exists today;
  this slice introduces the geometry-owned writer as new authority).
- No reader-side change: `LoadTGF` is unmodified and remains the
  ground-truth round-trip target.
- No GPU/Vulkan requirement in the default CPU gate.
- No new exporter for vertex/edge properties beyond the three the
  reader already round-trips (`v:label`, `e:weight`, `e:label`).
- No promotion of arbitrary user-defined property serialization in
  this slice.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-6ZFQb`.
- Parent backlog task:
  `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002J..M` shipped point-cloud and mesh writers symmetric to
  the existing readers in `Geometry.PointCloud.IO` /
  `Geometry.HalfedgeMesh.IO`. The graph IO module currently has only
  readers (`LoadTGF`, `LoadEdgeList`) and no writer surface, leaving
  graph round-trip parity unproven on the CPU-only path.
- The reader is in `src/geometry/Geometry.Graph.IO.cpp::LoadTGF`. It
  reads the vertex section as `id [x y z] [label...]`, switches on
  the first `#` line to the edge section, and reads each edge as
  `from_id to_id [weight] [label...]`. IDs are stored as strings in
  an `unordered_map`, with positions stored only when the three
  tokens following the ID parse as floats; otherwise position
  defaults to `(0,0,0)` and the label spans tokens [1..]. Edge weight
  is taken from token[2] only if it parses as a float; otherwise the
  label spans tokens [2..].
- The writer therefore emits each vertex as
  `<index> <x> <y> <z>[ <label>]` and each edge as
  `<from_index> <to_index>[ <weight>][ <label>]`, where indices are
  the original `VertexHandle::Index` values rendered as decimal
  strings. Using handle indices avoids label/id collisions and
  guarantees uniqueness even when vertices have been deleted from
  the source graph (the reader stores IDs as strings, so any unique
  printable token works).

## Required changes
- Extend `src/geometry/Geometry.Graph.IO.cppm`:
  - Add a `GraphIOWriteStatus` enum class in the
    `Geometry::GraphIO` namespace with values `Success`,
    `InvalidPath`, `EmptyGraph`, `FileWriteError`. Reuse the same
    naming pattern as `MeshIOWriteStatus` /
    `PointCloudIOWriteStatus`.
  - Add `GraphIOWriteStatus WriteTGF(std::string_view absolute_path,
                                      const GraphIOResult& graph);`
    declaration in the `Geometry::GraphIO` namespace.
- Implement `WriteTGF` in `src/geometry/Geometry.Graph.IO.cpp`:
  - Reject empty `absolute_path` with `InvalidPath`.
  - Reject graphs whose `Graph.VertexCount() == 0` with
    `EmptyGraph`. Empty edge sets are allowed (the reader only
    rejects empty graphs that have zero vertices).
  - Open the output stream with
    `std::ios::binary | std::ios::trunc`; return `InvalidPath` if
    the stream cannot be opened.
  - Iterate vertices in handle index order from `0` up to
    `VerticesSize()`, skipping `IsDeleted(VertexHandle)` entries.
    Emit `"<index> <x> <y> <z>"` using
    `std::snprintf` with `%.6f` for floats. If `v:label` is a
    valid `VertexProperty<std::string>` and the per-vertex label
    is non-empty, append `" "` followed by the label as-is (the
    reader joins tokens with spaces, so embedded spaces survive
    round-trip). Terminate each line with `\n`.
  - Emit a single `"#\n"` separator line after the vertex section,
    even when there are no edges (so the reader does not
    accidentally interpret edge lines as additional vertex lines).
  - Iterate edges in handle index order from `0` up to
    `EdgesSize()`, skipping `IsDeleted(EdgeHandle)` entries. For
    each edge, look up endpoints with `EdgeVertices(e)` and emit
    `"<from_index> <to_index>"`. If `e:weight` is a valid
    `EdgeProperty<float>`, append `" "` and the weight formatted
    via `%.6f`. If `e:label` is a valid
    `EdgeProperty<std::string>` and the per-edge label is
    non-empty, append `" "` and the label as-is. Terminate with
    `\n`.
  - Flush and report `FileWriteError` if `stream.good()` is false
    at end.
  - When an edge has a non-empty label but no weight, the reader
    interprets token[2] as a label only if it cannot be parsed as
    a number. The writer therefore always emits a numeric weight
    column when emitting a label, defaulting to the property's
    default value (`1.0f`) for edges that lack an explicit weight.
    This keeps the label round-trip unambiguous and matches the
    reader's `v:label`/`e:label`/`e:weight` contract.
- No additional public exports beyond `WriteTGF` and the new
  `GraphIOWriteStatus` enum; helper logic stays inside the
  existing translation-unit anonymous namespace or local to the
  function.
- Add `<cstdio>` and `<fstream>` to the implementation translation
  unit if not already pulled in transitively. Do not add new
  imports to the module interface.

## Tests
- Add `unit;geometry` cases to `tests/unit/geometry/Test.GeometryIO.cpp`
  under `GeometryIO_GraphIO`:
  - `WritesTGFRoundTripsVerticesAndEdges` — build a graph with two
    vertices and one edge (no labels, no weight); write via
    `WriteTGF`, re-import via `LoadTGF`, verify vertex count, edge
    count, and per-vertex positions.
  - `WritesTGFRoundTripsLabelsAndWeight` — build a graph with two
    vertices that have `v:label`, one edge with both `e:weight` and
    `e:label`; write, re-import, verify labels/weight round-trip
    exactly.
  - `WritesTGFEmitsSeparatorEvenWithoutEdges` — graph with one
    vertex and no edges; assert the on-disk file contains a `#`
    separator line and re-imports cleanly.
  - `WritesTGFSkipsDeletedVertices` — build a graph with three
    vertices, delete one, and verify the writer does not emit a
    line for the deleted vertex (re-import preserves the surviving
    vertex count).
  - `WriteTGFRejectsEmptyGraph` — default-constructed
    `GraphIOResult` (zero vertices) yields `EmptyGraph`.
  - `WriteTGFRejectsBadPath` — empty `absolute_path` yields
    `InvalidPath`; a path under a non-existent directory yields
    `InvalidPath`.
- Use the existing `TempFile` helper and `ReadFileContents` in the
  test file; do not introduce new test-only headers.

## Docs
- Add a `Graph exporters` row to
  `docs/migration/nonlegacy-parity-matrix.md` after the existing
  `TGF graph import` row, recording that the geometry-owned TGF
  writer was added under `GEOIO-002N`.
- Regenerate `docs/api/generated/module_inventory.md` only if the
  generator picks up changes to the existing
  `Geometry.Graph.IO` module surface. If the regenerator changes
  only the date stamp, leave it untouched.

## Acceptance criteria
- `Geometry::GraphIO::WriteTGF` and `Geometry::GraphIO::GraphIOWriteStatus`
  compile and are exported from `Geometry.Graph.IO`.
- New tests pass under `IntrinsicTests` and the CPU gate.
- No assets/runtime/graphics imports leak into `src/geometry/*`.
- Parity matrix gains a graph-export row reflecting the new
  geometry-owned writer.

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
- Adding assets/runtime/graphics/RHI imports to `src/geometry/*`.
- Adding an edge-list (`.edges`) or other graph-format writer in
  this slice.
- Changing the existing `LoadTGF` or `LoadEdgeList` reader signatures
  or behavior.
- Mixing mechanical legacy deletion with semantic IO implementation.
- Promoting arbitrary user-defined vertex/edge property
  serialization in this slice.

## Completion
- Completed: 2026-05-10.
- Status: done.
- Branch: `claude/setup-agentic-workflow-6ZFQb`.
- Implementation commit: `94c44c8`
  (`GEOIO-002N: add geometry-owned text TGF graph exporter`).
- Verified in this session:
  - `python3 tools/repo/check_layering.py --root src --strict` — no
    layering violations; `geometry` imports remain `geometry -> core`
    only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (143 task files validated).
  - `python3 tools/docs/check_doc_links.py --root .` — 0 broken
    relative links.
  - `python3 tools/repo/generate_module_inventory.py --root src
    --out docs/api/generated/module_inventory.md` — 420 modules; no
    diff (the new exported function and enum live inside the
    existing `Geometry.Graph.IO` module surface and the inventory
    tracks modules, not individual functions).
- CPU build/test gate not exercised: this container lacks
  `clang-20` and `clang-scan-deps` 20+, which the `ci` preset
  hard-codes. `cmake --preset ci` therefore fails at the compiler
  detection step, mirroring the constraint already recorded by
  `GEOIO-002L`/`GEOIO-002M` and earlier slices. Build verification
  therefore needs to be re-run on a CI host with the correct
  toolchain prior to merge.
- Notes:
  - `Geometry::GraphIO::WriteTGF` and the new
    `Geometry::GraphIO::GraphIOWriteStatus` enum are exported from
    `src/geometry/Geometry.Graph.IO.cppm` and implemented in
    `src/geometry/Geometry.Graph.IO.cpp`.
  - The on-disk encoding is plain ASCII text. Each surviving
    vertex emits one `<index> <x> <y> <z>[ <label>]\n` line where
    `<index>` is the source `VertexHandle::Index`, positions are
    formatted via `std::snprintf` using `%.6f`, and a trailing
    label is appended only when `v:label` is present and the
    per-vertex value is non-empty. A single `#\n` separator line
    follows the vertex section even when there are no edges, so
    the reader switches to edge-mode unambiguously. Each surviving
    edge emits `<from_index> <to_index>[ <weight>][ <label>]\n`,
    where the weight column is `%.6f` and is emitted whenever
    `e:weight` is present, or whenever an `e:label` would be
    emitted (using the property's default value `1.0f` if no
    weight property is set), so the reader's number-vs-label
    disambiguation at token[2] always parses as expected.
  - Deleted vertices and edges are skipped (their handle indices
    are not emitted), which means vertex IDs in the file may be
    non-contiguous; the reader stores IDs as strings and looks
    them up via an `unordered_map`, so non-contiguity is benign.
  - Empty vertex graphs (`Graph.VertexCount() == 0`) yield
    `EmptyGraph`. Empty edge sets are allowed because `LoadTGF`
    accepts files with no edges. Empty `absolute_path` and paths
    under non-existent directories yield `InvalidPath` (the
    `std::ofstream` open fails).
  - Tests in `tests/unit/geometry/Test.GeometryIO.cpp`
    (`GeometryIO_GraphIO`) add
    `WritesTGFRoundTripsVerticesAndEdges`,
    `WritesTGFRoundTripsLabelsAndWeight`,
    `WritesTGFEmitsSeparatorEvenWithoutEdges`,
    `WritesTGFSkipsDeletedVertices`,
    `WriteTGFRejectsEmptyGraph`, and `WriteTGFRejectsBadPath`.
  - `docs/migration/nonlegacy-parity-matrix.md` gains a new
    `TGF graph export` row recording the geometry-owned writer.
    No legacy TGF exporter exists, so this slice introduces the
    promoted writer as new authority rather than retiring a
    legacy one.
  - Remaining `GEOIO-002` scope (PCD point-cloud writer; granular
    reader-side `MeshIOReadStatus`/`PointCloudIOReadStatus`
    diagnostics; OBJ ASCII parity hardening; packed-`rgb`/`rgba`
    PCD plus `binary_compressed` LZF decompression; binary STL
    mesh writer) stays tracked under the parent backlog task
    `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
