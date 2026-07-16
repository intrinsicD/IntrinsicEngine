---
id: BUG-099
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-099 — Binary PLY point-cloud import rejects face-list elements

## Status

- Completed on 2026-07-16 at `CPUContracted`; implementation commit:
  `bb985f61`.
- `IntrinsicGeometryTests` built; all 78 point-cloud IO cases and the focused
  17-test binary PLY selection passed. The aggregate build and default
  CPU-supported gate passed 3,830/3,830; strict structural and documentation
  checks passed.
- `ASSETIO-011` retains the real Sandbox `Operational` matrix follow-up.

## Goal

- Let the promoted binary PLY point-cloud reader decode vertex data while
  safely consuming unrelated scalar and list-valued elements such as mesh
  faces in both little- and big-endian files.

## Non-goals

- No conversion of PLY faces into point-cloud topology or mesh data.
- No acceptance of list-valued properties on the `vertex` element.
- No relaxed bounds checking, truncated-body tolerance, or unchecked list
  count arithmetic.
- No format auto-selection change; `.ply` still requires an explicit Mesh or
  PointCloud payload in the Sandbox.
- No committed dependency on local user datasets.

## Context

- Symptom: both checked-in `__test_triangle_le.ply` and
  `__test_triangle_be.ply` import visibly as Mesh, but choosing the advertised
  PointCloud payload fails with `InvalidFormat`.
- Expected behavior: a valid PLY may contain vertex data plus later face or
  application-specific elements. The point-cloud reader should retain only
  supported vertex properties while consuming every declared binary row so
  stream alignment and truncation validation remain exact.
- Root cause: `ParseBinaryPLYPointCloud()` rejects any list property on every
  non-vertex element instead of reading the list count and skipping its
  declared values. The mesh reader already demonstrates that the fixtures are
  valid binary PLY in both endian modes.
- Owner: the private implementation of `Geometry.PointCloud.IO`; no runtime,
  assets, ECS, graphics, or app dependency is required.
- Live evidence on 2026-07-16 exercised Unknown -> explicit Mesh success ->
  explicit PointCloud failure through the real File / Import controls for
  both checked-in endian fixtures.

## Required changes

- [x] Add one bounds-checked binary property consumer for non-vertex PLY rows:
      fixed scalars advance by their declared width; list properties decode an
      integral count in the file's endian order and advance by
      `count * elementWidth` with overflow/truncation checks.
- [x] Reject negative, floating, overflowing, or truncated list counts and
      retain strict rejection of vertex-list properties.
- [x] Preserve element order and cursor alignment when scalar and list
      properties are interleaved or when multiple non-vertex elements exist.
- [x] Keep the public point-cloud IO module surface unchanged.

## Tests

- [x] Add generated little- and big-endian point-cloud PLY fixtures whose
      vertex element is followed by a valid face index list; assert exact
      positions and successful decode.
- [x] Add mixed scalar/list non-vertex coverage and malformed cases for
      truncated count, truncated payload, negative signed count, floating
      count type, and multiplication overflow where representable.
- [x] Preserve the existing regression that rejects a list property inside
      the vertex element.
- [x] Exercise the two checked-in endian fixtures through focused geometry IO
      coverage without making screenshot evidence the durable test oracle.

## Docs

- [x] Update the geometry IO support notes to state that point-cloud PLY reads
      ignore but strictly consume non-vertex elements, including lists.
- [x] Refresh task indexes/session brief and retirement records on closure.

## Acceptance criteria

- [x] Both endian variants decode valid vertex-plus-face-list PLY as a point
      cloud with the exact declared vertices.
- [x] Malformed list counts or payloads fail closed without out-of-bounds reads
      or cursor desynchronization.
- [x] Mesh PLY behavior and vertex-list rejection remain unchanged.
- [x] The focused geometry unit suite and default CPU-supported gate pass.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure \
  -R '^GeometryIO_PointCloudIO\.(LoadsBinary.*PLY.*|LoadsCheckedInBinaryPLY.*|LoadPLYPointCloudRejectsTruncatedBinaryBody|BinaryPLYPointCloudRejects.*|LoadPLYPointCloudRejectsListPropertyInVertex)$' \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Treating the entire remaining body as ignorable bytes without parsing its
  declared row/property layout.
- Reading a list count or payload before proving enough bytes remain.
- Accepting list-valued vertex positions/normals/colors.
- Solving the bug in runtime routing or with fixture-specific filename logic.

## Maturity

- Achieved: `CPUContracted` for the geometry-owned format contract;
  `Operational` owned by `ASSETIO-011` through the real Sandbox control
  matrix.
