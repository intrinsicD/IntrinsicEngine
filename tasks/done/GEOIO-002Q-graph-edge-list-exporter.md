# GEOIO-002Q — Geometry-owned text edge-list graph exporter

## Goal
- Add a geometry-owned text edge-list (`.edges`) exporter API to
  `Geometry.Graph.IO` that serializes a `GraphIOResult`
  (mandatory edges; optional `e:weight` and `e:label`
  properties) without introducing assets/runtime/graphics
  dependencies, so the broader `GEOIO-002` parity work can grow
  graph round-trip coverage symmetric to the existing edge-list
  importer (`Geometry::GraphIO::LoadEdgeList`).

## Non-goals
- No new graph format beyond text edge-list in this slice; TGF
  was added under `GEOIO-002N` and is the only other graph writer
  currently in scope.
- No assets/runtime ownership of graph file IO; geometry owns
  format codecs only.
- No new format-detection metadata helpers.
- No legacy graphics edge-list exporter retirement (none exists
  today; this slice introduces the geometry-owned writer as new
  authority).
- No reader-side change: `LoadEdgeList` is unmodified and remains
  the ground-truth round-trip target.
- No GPU/Vulkan requirement in the default CPU gate.
- No promotion of arbitrary user-defined vertex/edge property
  serialization beyond the `e:weight` and `e:label` properties
  already round-tripped by the reader.
- No preservation of isolated vertices. Edge-list format
  represents the graph as the union of its edges; vertices not
  incident to any surviving edge are not representable and are
  silently omitted (consistent with the reader, which only
  materializes vertices observed in edge endpoints).
- No preservation of the original `v:id` strings. Symmetric to
  the TGF writer in `GEOIO-002N`, the writer emits the
  `VertexHandle::Index` as the on-disk ID; the reader will
  re-create `v:id` from those decimal strings on round trip.

## Context
- Status: done.
- Owner/agent: `geometry -> core` only.
- Branch: `claude/setup-agentic-workflow-hLR8v`.
- Parent backlog task:
  `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
- `GEOIO-002N` shipped the symmetric TGF graph writer
  (`Geometry::GraphIO::WriteTGF`) and introduced the
  `GraphIOWriteStatus` enum (`Success`, `InvalidPath`,
  `EmptyGraph`, `FileWriteError`). This slice reuses the same
  enum unchanged.
- The reader is in
  `src/geometry/Geometry.Graph.IO.cpp::LoadEdgeList`. It reads
  each non-comment, non-empty line as
  `from_id to_id [weight] [label]`. IDs are stored as strings in
  an `unordered_map`, with vertices materialized on first
  reference (position defaults to `(0, 0, 0)`; `v:id` records the
  string ID). Mid-line `#` strips the rest of the line as a
  comment. Edge weight is taken from token[2] only if it parses
  as a float; otherwise the label spans tokens [2..]. The reader
  rejects files that produce zero vertices or zero edges with
  `Core::ErrorCode::InvalidFormat`.
- The writer therefore emits each surviving edge as
  `<from_index> <to_index>[ <weight>][ <label>]` where
  `<from_index>` and `<to_index>` are the source
  `VertexHandle::Index` values rendered as decimal strings. Using
  handle indices avoids label/id collisions and guarantees
  uniqueness even when vertices have been deleted from the source
  graph (the reader stores IDs as strings, so any unique
  printable token works).

## Required changes
- Extend `src/geometry/Geometry.Graph.IO.cppm`:
  - Add a `GraphIOWriteStatus WriteEdgeList(std::string_view absolute_path,
                                              const GraphIOResult& graph);`
    declaration in the `Geometry::GraphIO` namespace, reusing the
    existing `GraphIOWriteStatus` enum unchanged.
- Implement `WriteEdgeList` in
  `src/geometry/Geometry.Graph.IO.cpp` adjacent to the existing
  `WriteTGF`:
  - Reject empty `absolute_path` with `InvalidPath`.
  - Reject graphs whose `Graph.VertexCount() == 0` or whose
    `Graph.EdgeCount() == 0` with `EmptyGraph`. The reader
    rejects both conditions, so the writer mirrors that contract
    and avoids producing files that cannot be re-imported.
  - Open the output stream with
    `std::ios::binary | std::ios::trunc`; return `InvalidPath` if
    the stream cannot be opened.
  - Iterate edges in handle index order from `0` up to
    `EdgesSize()`, skipping `IsDeleted(EdgeHandle)` entries. For
    each edge, look up endpoints with `EdgeVertices(e)` and emit
    `"<from_index> <to_index>"` using `std::snprintf`. If
    `e:weight` is a valid `EdgeProperty<float>`, append `" "` and
    the weight formatted via `%.6f`. If `e:label` is a valid
    `EdgeProperty<std::string>` and the per-edge label is
    non-empty, append `" "` and the label as-is. Terminate each
    line with `\n`.
  - When an edge has a non-empty label but no weight, the reader
    interprets token[2] as a label only if it cannot be parsed as
    a number. The writer therefore always emits a numeric weight
    column when emitting a label, defaulting to the property's
    default value (`1.0f`) for edges that lack an explicit weight.
    This keeps the label round-trip unambiguous and matches the
    reader's `e:weight`/`e:label` contract.
  - Flush and report `FileWriteError` if `stream.good()` is false
    at end.
- No additional public exports beyond `WriteEdgeList`; helper
  logic stays inside the existing translation-unit anonymous
  namespace or local to the function.
- No new imports on the module interface; `<cstdio>` and
  `<fstream>` are already pulled in by the existing `WriteTGF`
  translation unit.

## Tests
- Add `unit;geometry` cases to
  `tests/unit/geometry/Test.GeometryIO.cpp` under
  `GeometryIO_GraphIO`:
  - `WritesEdgeListRoundTripsEdges` — build a graph with three
    vertices and two edges (no labels, no weight); write via
    `WriteEdgeList`, re-import via `LoadEdgeList`, verify vertex
    count, edge count, and that endpoint IDs (`v:id`) match the
    original handle indices rendered as decimal strings.
  - `WritesEdgeListRoundTripsLabelsAndWeight` — build a graph
    with two vertices and one edge that has both `e:weight` and
    `e:label`; write, re-import, verify label and weight round-
    trip exactly.
  - `WritesEdgeListEmitsWeightWhenLabelOnly` — build a graph with
    an edge that has only `e:label` (no `e:weight` property at
    all); write, assert the on-disk line contains a numeric
    weight column before the label so the reader's
    number-vs-label disambiguation parses the label correctly;
    re-import and verify the label round-trips.
  - `WritesEdgeListSkipsDeletedEdges` — build a graph with three
    vertices and two edges, delete one edge, and verify the
    writer does not emit a line for the deleted edge (re-import
    preserves the surviving edge count).
  - `WriteEdgeListRejectsEmptyGraph` — default-constructed
    `GraphIOResult` (zero vertices) yields `EmptyGraph`.
  - `WriteEdgeListRejectsEdgelessGraph` — a graph with vertices
    but no edges yields `EmptyGraph` (mirrors the reader's
    rejection of edge-less files).
  - `WriteEdgeListRejectsBadPath` — empty `absolute_path` yields
    `InvalidPath`; a path under a non-existent directory yields
    `InvalidPath`.
- Use the existing `TempFile` helper in the test file; do not
  introduce new test-only headers.

## Docs
- Update the `TGF graph export` row of
  `docs/migration/nonlegacy-parity-matrix.md` (or add an adjacent
  row) to record that text edge-list (`.edges`) graph export is
  now geometry-owned and added under `GEOIO-002Q`.
- Regenerate `docs/api/generated/module_inventory.md` only if the
  generator picks up changes to the existing
  `Geometry.Graph.IO` module surface. If the regenerator changes
  only the date stamp, leave it untouched.

## Acceptance criteria
- `Geometry::GraphIO::WriteEdgeList` compiles and is exported
  from `Geometry.Graph.IO`.
- New tests pass under `IntrinsicTests` and the CPU gate.
- No assets/runtime/graphics imports leak into `src/geometry/*`.
- Parity matrix records the new geometry-owned edge-list writer.

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
- Changing the existing `LoadTGF`, `LoadEdgeList`, or `WriteTGF`
  signatures or behavior.
- Mixing mechanical legacy deletion with semantic IO
  implementation.
- Promoting arbitrary user-defined vertex/edge property
  serialization in this slice.
- Emitting isolated (edge-less) vertices into the edge-list file
  via synthesized self-loops or any other workaround.

## Completion
- Completed: 2026-05-10.
- Status: done.
- Branch: `claude/setup-agentic-workflow-hLR8v`.
- Implementation commit: `27d33f8`
  (`GEOIO-002Q: add geometry-owned text edge-list graph exporter`).
- Verified in this session:
  - `python3 tools/repo/check_layering.py --root src --strict` —
    no layering violations; `geometry` imports remain
    `geometry -> core` only.
  - `python3 tools/repo/check_test_layout.py --root . --strict` —
    0 findings.
  - `python3 tools/agents/check_task_policy.py --root . --strict` —
    0 findings (146 task files validated before the move; the
    file rename does not change the file count after the move).
  - `python3 tools/docs/check_doc_links.py --root .` — 0 broken
    relative links.
  - `python3 tools/repo/generate_module_inventory.py --root src
    --out docs/api/generated/module_inventory.md` — 420 modules;
    no diff (the new exported function lives inside the existing
    `Geometry.Graph.IO` module surface and the inventory tracks
    modules, not individual functions).
- CPU build/test gate not exercised: this container lacks
  `clang-20` and `clang-scan-deps` 20+, which the `ci` preset
  hard-codes. `cmake --preset ci` therefore fails at the compiler
  detection step, mirroring the constraint already recorded by
  `GEOIO-002L`/`GEOIO-002M`/`GEOIO-002N`/`GEOIO-002O`/`GEOIO-002P`
  and earlier slices. Build verification therefore needs to be
  re-run on a CI host with the correct toolchain prior to merge.
- Notes:
  - `Geometry::GraphIO::WriteEdgeList` is exported from
    `src/geometry/Geometry.Graph.IO.cppm` and implemented in
    `src/geometry/Geometry.Graph.IO.cpp`. It reuses the existing
    `GraphIOWriteStatus` enum (`Success`, `InvalidPath`,
    `EmptyGraph`, `FileWriteError`) introduced by `GEOIO-002N`.
  - The on-disk encoding is plain ASCII text. Each surviving edge
    emits one `<from_index> <to_index>[ <weight>][ <label>]\n`
    line where the indices are the source `VertexHandle::Index`
    values rendered as decimal strings, the weight column is
    `%.6f`, and a trailing label is appended only when `e:label`
    is present and the per-edge value is non-empty. The weight
    column is emitted whenever `e:weight` is present, or whenever
    an `e:label` would be emitted (using the property's default
    value `1.0f` if no weight property is set), so the reader's
    number-vs-label disambiguation at token[2] always parses as
    expected.
  - Deleted edges are skipped (their handle indices are not
    emitted), which means edge IDs in the file may be
    non-contiguous; the reader stores IDs as strings and looks
    them up via an `unordered_map`, so non-contiguity is benign.
    Isolated vertices (no surviving incident edge) are silently
    omitted because edge-list format only represents the union of
    its edges.
  - Empty vertex graphs (`Graph.VertexCount() == 0`) and
    edge-less graphs (`Graph.EdgeCount() == 0`) yield
    `EmptyGraph`. Empty `absolute_path` and paths under
    non-existent directories yield `InvalidPath` (the
    `std::ofstream` open fails).
  - Tests in `tests/unit/geometry/Test.GeometryIO.cpp`
    (`GeometryIO_GraphIO`) add
    `WritesEdgeListRoundTripsEdges`,
    `WritesEdgeListRoundTripsLabelsAndWeight`,
    `WritesEdgeListEmitsWeightWhenLabelOnly`,
    `WritesEdgeListSkipsDeletedEdges`,
    `WriteEdgeListRejectsEmptyGraph`,
    `WriteEdgeListRejectsEdgelessGraph`, and
    `WriteEdgeListRejectsBadPath`.
  - `docs/migration/nonlegacy-parity-matrix.md` gains a new
    `Edge-list (.edges) graph export` row recording the
    geometry-owned writer. No legacy edge-list exporter exists,
    so this slice introduces the promoted writer as new
    authority rather than retiring a legacy one.
  - Remaining `GEOIO-002` scope (granular reader-side
    `MeshIOReadStatus`/`PointCloudIOReadStatus` diagnostics;
    OBJ ASCII parity hardening; packed-`rgb`/`rgba` PCD plus
    `binary_compressed` LZF decompression; binary PCD writer)
    stays tracked under the parent backlog task
    `tasks/backlog/geometry/GEOIO-002-geometry-io-parity-hardening.md`.
