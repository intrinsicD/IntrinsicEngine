---
id: GEOM-051
theme: none
depends_on: [GEOM-033]
maturity_target: CPUContracted
---
# GEOM-051 — Property system enhancements — live-element iterator and upload metadata

## Goal
- Add a deleted-skipping live-element iterator to the property/half-edge system
  so callers can range-for over live handles without GC, an index loop, and a
  manual `IsDeleted` test, matching the tombstone-skipping behavior the topology
  circulators already provide for ring traversals.
- Keep GPU-upload-oriented `PropertyVector` metadata (dirty tracking via a
  `NeedsUpload` flag, N-dimensional element layout via `dims()`, and a
  scalar-type reflection enum) strictly gated behind a real GPU-upload consumer;
  do not land speculative GPU plumbing in this task.

## Non-goals
- Do not restructure the type-erased SoA property storage; the GEOM-027..034
  sequence owns `Geometry.Properties` storage and metadata.
- No GPU backend wiring, buffer residency, or upload path implementation.
- No renderer, runtime, ECS, UI, asset, platform, or app changes.
- No replacement of the existing topology circulators in `Geometry.Circulators`
  or the typed `Property<T>` / `ConstProperty<T>` accessors.
- No change to `GarbageCollection` semantics or the `m_VDeleted` / `m_EDeleted` /
  `m_FDeleted` deletion-marker storage.

## Context
- Owning subsystem/layer: `geometry` (`geometry -> core` only). Affected modules
  are `Geometry.Properties`, `Geometry.HalfedgeMesh`, and `Geometry.DomainViews`.
- Today `Geometry.HalfedgeMesh` exposes `VerticesSize()`, `FacesSize()`,
  `IsDeleted(VertexHandle)`, etc., and `HasGarbage()`, but there is no linear
  iterator that walks only live elements. Callers must loop over `[0, *Size())`
  and skip with `IsDeleted` by hand, or force `GarbageCollection()` first to
  compact tombstones. The topology circulators in `Geometry.Circulators`
  (`RingIterator` + `TraversalSentinel`) already skip deleted elements during
  ring traversal; the linear domain has no equivalent.
- This ports `bcg_property.h`'s `PropertyIterator`: a range-for over live handles
  that automatically skips tombstones, computed from the deletion markers without
  requiring compaction.
- The gated piece ports `bcg_property.h`'s `PropertyVector` upload metadata. It
  builds on the GEOM-033 erased property metadata catalog (`PropertyDescriptor`
  enumeration over `PropertySet` / `ConstPropertySet`) rather than introducing a
  second metadata path. The live-element iterator is the load-bearing deliverable;
  the upload metadata is optional and must remain consumer-gated.
- Coordinate with GEOM-027..034: do not duplicate or fork the erased storage
  metadata those tasks own.

## Slice plan
- [ ] **Slice A.** Add the deleted-skipping live-element iterator and a range
      adaptor to `Geometry.Properties`, parameterized by handle type and a
      deletion predicate, with fail-closed behavior on missing storage.
- [ ] **Slice B.** Expose live-element ranges on `Geometry.HalfedgeMesh`
      (vertices/edges/halfedges/faces) and on the `Geometry.DomainViews`
      read-only views, delegating to the Slice A adaptor.
- [ ] **Slice C.** Add focused `unit;geometry` tests proving the iterator
      matches the index-loop + `IsDeleted` result and handles empty/all-deleted
      containers; update geometry docs and regenerate the module inventory.
- [ ] **Slice D (gated, do NOT start without a consumer).** Only when a real
      GPU-upload consumer exists, add `PropertyVector` `NeedsUpload` dirty
      tracking, `dims()` N-dim layout, and the scalar-type reflection enum,
      layered on the GEOM-033 descriptor catalog.

## Required changes
- [ ] In [`src/geometry/Geometry.Properties.cppm`](../../../src/geometry/Geometry.Properties.cppm),
      export a deleted-skipping live-element iterator type and a range adaptor
      (e.g. `LiveElementIterator<HandleT>` and `LiveElementRange<HandleT>`)
      yielding `HandleT` values for live (non-tombstone) elements in ascending
      index order. The range takes an element count and a deletion predicate
      `bool(HandleT)` so it can be driven by any domain's deletion markers.
- [ ] Model the iterator as an `input_iterator` paired with a sentinel, mirroring
      the existing `Geometry.Circulators` `RingIterator` / `TraversalSentinel`
      style, so it composes with C++20 range-for; advancing skips tombstones and
      stops exactly at the element count.
- [ ] Keep non-trivial bodies out of the `.cppm`: place any non-inline iterator
      advance/seek logic in
      [`src/geometry/Geometry.Properties.cpp`](../../../src/geometry/Geometry.Properties.cpp);
      keep only the exported declarations and small inline/template glue in the
      interface unit.
- [ ] Fail closed: constructing a live range against a deletion predicate that
      cannot be evaluated (e.g. an invalid/empty domain) yields an empty range
      that compares equal to its sentinel immediately; no asserts, no UB, no
      reliance on `GarbageCollection` having run.
- [ ] In [`src/geometry/Geometry.HalfedgeMesh.cppm`](../../../src/geometry/Geometry.HalfedgeMesh.cppm),
      add live-element range accessors (e.g. `Vertices()`, `Edges()`,
      `Halfedges()`, `Faces()` live ranges, named to avoid collision with the
      existing `PropertySet` aliases) that build a `LiveElementRange` from the
      matching `*Size()` and `IsDeleted(...)` markers and skip deleted elements
      without compaction.
- [ ] Implement the half-edge live-range bodies in
      [`src/geometry/Geometry.HalfedgeMesh.cpp`](../../../src/geometry/Geometry.HalfedgeMesh.cpp);
      respect the submesh-view ranges (`m_VertexRange` / `m_FaceRange`) already
      used by `VerticesSize()` / `FacesSize()` so live iteration over a submesh
      view stays inside the borrowed window.
- [ ] In [`src/geometry/Geometry.DomainViews.cppm`](../../../src/geometry/Geometry.DomainViews.cppm),
      expose matching const live-element ranges on the read-only views
      (`ConstMeshBackedGraphView`, `ConstMeshBackedCloudView`,
      `ConstGraphBackedCloudView`) that delegate to the underlying domain's
      live range and `IsDeleted` markers; do not add any mutating accessor.
- [ ] Do not introduce any new module dependency: `Geometry.Properties` must not
      import `Geometry.HalfedgeMesh` or higher; the iterator stays generic and is
      specialized at the call site by passing the domain's deletion predicate.
- [ ] **Gated (Slice D only, requires a landed GPU-upload consumer):** in
      `Geometry.Properties`, add to the typed storage a `NeedsUpload` dirty flag
      with set/clear accessors, a `dims()` element-layout query, and a scalar-type
      reflection enum, exposed through the GEOM-033 `PropertyDescriptor` catalog
      rather than a parallel metadata structure. If no such consumer exists, this
      change is explicitly out of scope and must not be added.

## Tests
- [ ] Add a `unit;geometry` test in a new file (e.g.
      `tests/unit/geometry/Test_PropertyLiveElementIterator.cpp`, labeled
      `unit;geometry`) covering the iterator on a `PropertySet`-backed domain.
- [ ] Assert the live iterator visits exactly the non-deleted elements, in
      ascending index order, after a mix of deletions, without calling
      `GarbageCollection`.
- [ ] Assert the live iteration result is element-for-element identical to a
      manual `for (i in [0, Size())) if (!IsDeleted(handle(i)))` loop over the
      same domain.
- [ ] Assert an empty container iterates to zero elements (begin compares equal
      to the sentinel) and an all-deleted container also iterates to zero
      elements.
- [ ] Add half-edge mesh coverage in
      [`tests/unit/geometry/Test_HalfedgeMeshPropertyAccess.cpp`](../../../tests/unit/geometry/Test_HalfedgeMeshPropertyAccess.cpp)
      (or a sibling `unit;geometry` file) proving the mesh live ranges for
      vertices/edges/halfedges/faces skip deleted elements after edge/vertex/face
      deletion and match the index-loop + `IsDeleted` baseline.
- [ ] Add a submesh-view test proving live iteration over a borrowed window
      stays within the view's `ElementRange` and does not escape into out-of-window
      storage.
- [ ] Add a `Geometry.DomainViews` test proving the const views expose live
      ranges that agree with the underlying domain and offer no mutating handle.
- [ ] Add a degenerate-input test proving a live range built against an
      invalid/empty domain fails closed to an empty range with no asserts.
- [ ] **Gated (Slice D only):** if the upload metadata is implemented, add tests
      proving the `NeedsUpload` flag sets on mutation and clears on acknowledge,
      and that `dims()` and the scalar-type enum reflect the stored layout for
      `float`, `glm::vec3`, and at least one integral property.

## Docs
- [ ] Update
      [`docs/architecture/geometry.md`](../../../docs/architecture/geometry.md)
      with the live-element iterator contract (tombstone-skipping, no GC required,
      submesh-window respect).
- [ ] Update
      [`docs/architecture/geometry-api-style.md`](../../../docs/architecture/geometry-api-style.md)
      to document the linear live-range idiom alongside the existing circulator
      ring-traversal idiom and the index-loop + `IsDeleted` fallback it replaces.
- [ ] Regenerate
      [`docs/api/generated/module_inventory.md`](../../../docs/api/generated/module_inventory.md)
      because this changes the exported surface of `Geometry.Properties`,
      `Geometry.HalfedgeMesh`, and `Geometry.DomainViews`.
- [ ] If new CTest labels were needed, update
      [`tests/README.md`](../../../tests/README.md) and
      [`tests/CMakeLists.txt`](../../../tests/CMakeLists.txt) in this change;
      otherwise reuse the existing `unit;geometry` label and add no new label.
- [ ] **Gated (Slice D only):** document the upload metadata and its
      consumer-gating only if implemented; do not document an unimplemented GPU
      path.

## Acceptance criteria
- [ ] A caller can write a range-for over a domain's live handles and observe
      exactly the non-deleted elements, in ascending order, with no call to
      `GarbageCollection` and no manual `IsDeleted` test.
- [ ] Live iteration is provably equal to the index-loop + `IsDeleted` baseline
      for the same domain state, including after interleaved deletions.
- [ ] Empty and all-deleted domains iterate to zero elements; a live range over
      an invalid/empty domain fails closed to an empty range with explicit,
      assert-free behavior.
- [ ] Live iteration over a submesh view stays inside the view's `ElementRange`.
- [ ] The read-only `Geometry.DomainViews` views expose live ranges and hand out
      no mutable accessor through them.
- [ ] `geometry -> core` layering is preserved; `Geometry.Properties` gains no
      dependency on `Geometry.HalfedgeMesh` or any higher layer.
- [ ] No GPU-upload metadata is present unless a real GPU-upload consumer landed
      in the same change (Slice D); otherwise the upload metadata is absent.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PropertyLiveElement|HalfedgeMeshPropertyAccess|DomainBorrows|Properties' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Introducing renderer/runtime/ECS/assets/platform/app dependencies from geometry.
- Claiming performance improvements without a baseline comparison.
- Adding GPU-upload `PropertyVector` metadata (dirty flag, `dims()`, scalar-type
  enum) without a real GPU-upload consumer landing in the same change.
- Restructuring the type-erased SoA property storage owned by GEOM-027..034.
- Changing `GarbageCollection` semantics or the deletion-marker storage.
- Introducing a new CTest label without updating `tests/README.md` and
  `tests/CMakeLists.txt` in the same change.

## Maturity
- Target: `CPUContracted`. The live-element iterator is a geometry-owned CPU
  contract with deterministic, fail-closed behavior and CPU `unit;geometry`
  tests; it closes at `CPUContracted` once the iterator and its mesh/view
  ranges are tested against the index-loop + `IsDeleted` baseline.
- The GPU-upload metadata (Slice D) is intentionally not part of this stop-state.
  Any `Operational` / `ParityProven` follow-up for upload tracking is owed only
  when a real GPU-upload consumer task exists and must land under that task.

- Closure: no `Operational` follow-up is owed for this task.
