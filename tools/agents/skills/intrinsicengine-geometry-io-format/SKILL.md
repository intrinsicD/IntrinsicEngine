---
name: intrinsicengine-geometry-io-format
description: The proven slice template for adding a geometry importer or exporter to IntrinsicEngine so a new format instantiates the shape instead of re-deriving it — geometry→core layering with parsers in an anonymous namespace behind an unchanged .cppm surface, Core::Expected<...Result> readers and *IOWriteStatus-enum writers, strict parsing with untrusted-header-count validation bounded against the payload before any reserve/read, an explicit diagnostics taxonomy, and unit;geometry determinism + round-trip + truncated/non-finite fail-closed tests with committed or byte-level fixtures. Use this skill whenever adding or changing a mesh/point-cloud/graph importer or exporter (OBJ/OFF/PLY/STL/PCD/XYZ/TGF/edge-list), parsing an untrusted header count or binary body, defining IO diagnostics/status values, writing geometry-IO fixtures or round-trip tests, or deciding the CPUContracted closure wording for a format slice.
---

# IntrinsicEngine Geometry IO Format Slice

This skill is the template for a geometry importer/exporter slice. The identical
shape was instantiated ~35 times across the `GEOIO-002A`..`GEOIO-002AG` and
`GEOIO-003` series; a new format should copy the proven shape, not re-derive it.
It codifies conventions that already hold in the tree — it adds no new behavior.

Owner layer: `geometry -> core` **only**. A format slice never adds
`assets`/`runtime`/`graphics`/`RHI` imports; asset/runtime routing and legacy
graphics-importer retirement are separate tasks. This skill owns the
parser/exporter slice shape; the runtime materialization/visibility contract is
`intrinsicengine-import-visibility-contract` (`PROC-018`).

## The slice template

### 1. Layering and module surface

- Keep the public `.cppm` surface stable. Parsers/encoders land as
  anonymous-namespace helpers in the matching `.cpp`
  (`Geometry.HalfedgeMesh.IO.cpp`, `Geometry.PointCloud.IO.cpp`, …); adding
  internal helpers and expanding a function body must not change the module
  inventory. Cited exemplars: `GEOIO-002D`, `GEOIO-002E`.
- Reuse existing IO primitives rather than adding a helper for a single use
  site — e.g. `Geometry::IOText::ReadTextFile` already opens `std::ios::binary`,
  so a binary parser takes a `std::span<const std::byte>` view over its buffer.

### 2. API shape

- **Readers** return `Core::Expected<...Result>` (e.g.
  `Core::Expected<MeshIOResult> LoadPLY(std::string_view absolute_path)`).
- **Writers** return a format-family write-status enum:
  `MeshIOWriteStatus` / `PointCloudIOWriteStatus` / `GraphIOWriteStatus`
  (values include a success state, `EmptyMesh`, `InvalidFace`, `InvalidPath`,
  `FileWriteError`).
- A new encoding of an existing format extends the existing `Load*`/`Write*`
  entry point (dispatch inside), it does not add a parallel public function
  unless the format is genuinely new.

### 3. Strict parsing + untrusted-header-count validation

This is the load-bearing safety rule (origin: `BUG-033`). Never trust a declared
count from the file:

- Bound every declared count against the **actual remaining payload** before any
  `reserve`/allocation or element read: reject `count == 0`, reject
  `size < header + count * record_stride`, reject list arity below the minimum
  (e.g. a face list count `< 3`), and reject non-integral/negative list counts.
- Use overflow-safe byte arithmetic; decode fixed-size scalars via `std::memcpy`
  (+ explicit byte-swap for declared big-endian), never reinterpret_cast over
  unaligned bytes.
- A malformed/truncated/oversized body must fail closed, never partial-parse or
  read out of bounds. Cited exemplars: `GEOIO-002D` (binary STL),
  `GEOIO-002E` (binary PLY, extra vertex scalars skipped by stride).

### 4. Diagnostics taxonomy

- Reader failures surface as `Core::ErrorCode` through the `Core::Expected`
  channel — the house helper is `InvalidMeshFormat()`
  (`Core::ErrorCode::InvalidFormat`) for every malformed-input class. A granular
  per-reason reader enum (`MeshIOReadStatus`) is deliberately deferred future
  scope, not part of a format slice.
- Writer failures use the explicit `*IOWriteStatus` value for the reason
  (`EmptyMesh`, `InvalidFace`, `InvalidPath`, `FileWriteError`).
- Point-cloud/graph list-diagnostic slices (e.g. `GEOIO-002AD`) still route
  through the same `Core::ErrorCode` channel; add the reason to the taxonomy the
  format family already uses rather than inventing a one-off status.

### 5. Tests (`unit;geometry`, in `tests/unit/geometry/Test.GeometryIO.cpp`)

Every slice ships the same test classes:

- **Round-trip** — for exporters, write a synthetic `MeshIOResult`, re-import via
  the matching `Load*`, and assert topology + vertex equivalence (including face
  arity, e.g. a quad survives as arity 4).
- **Determinism** — the same input produces byte-identical output (modulo
  declared nondeterminism).
- **Fail-closed** — a truncated/oversized/malformed payload yields
  `Core::ErrorCode::InvalidFormat` (readers) or the documented `*WriteStatus`
  (writers: empty mesh, out-of-range index, non-writable path); non-finite input
  is rejected, not silently emitted.
- **Regression** — an existing fixture for the format still parses after any
  dispatch refactor (`Loads…AfterBinaryDispatch` pattern).
- **Fixtures** — small text fixtures are committed under
  `tests/support/geometry/geometry_io/`; binary fixtures are produced by an
  in-test `WriteBinary…Fixture` helper using `std::ofstream(..., std::ios::binary)`
  with explicit byte-level writes and manual byte-swap for big-endian variants —
  never `operator<<` for binary scalars.

### 6. Closure wording

A format slice closes at **`CPUContracted`**: the `unit;geometry` contract tests
pass under the default CPU gate. State explicitly in the task whether an
`Operational` follow-up is owed — for pure geometry IO it usually is **not**
(`no `Operational` follow-up is owed`), because there is no backend to prove;
asset/runtime routing that would make the format visible is a separate task. If
the container cannot run `cmake --preset ci`, report that honestly and record
the non-build structural gates that did run (`check_layering`, `check_task_policy`,
`check_test_layout`, module-inventory regeneration).

## Exemplars to copy

- `GEOIO-002B` — mesh PLY ASCII exporter: `MeshIOWriteStatus`, round-trip and
  reject-empty/bad-index/bad-path tests.
- `GEOIO-002D` — binary STL importer: untrusted `triCount` bounded against
  payload, truncated-payload fail-closed test, anonymous-namespace dispatch.
- `GEOIO-002E` — binary PLY importer (little/big-endian): structured header
  parse, stride-skip of extra vertex scalars, list-arity and truncation
  rejection.

## Related

- `intrinsicengine-import-visibility-contract` (`PROC-018`) — the runtime
  materialization/visibility contract this skill deliberately does not own.
- `intrinsicengine-task-workflow` — the maturity taxonomy and the
  `CPUContracted` closure/`Scaffolded`-closure rules referenced above.
- `intrinsicengine-core` — the `geometry -> core` layering invariant and the
  `.cppm` interface/implementation split.
