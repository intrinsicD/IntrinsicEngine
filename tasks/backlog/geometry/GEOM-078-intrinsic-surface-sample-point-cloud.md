---
id: GEOM-078
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Operator-requested interactive design note; implementation evidence will be the diff, focused tests, and CI.
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation]
---
# GEOM-078 — Intrinsic surface-sample point cloud with contiguous face ranges

## Goal

Represent multiple independent points on each triangle as an intrinsic 2D
surface-sample point cloud attached to the mesh entity, retaining both
sample-to-face ownership and direct face-to-sample iteration.

## Context and decisions — 2026-09-17

- The operator requested this task while discussing K-Means on points located
  inside mesh triangles. This is explicitly requested foundation work outside
  the standing Framework24 convergence selection preference.
- Each sample has a local 2D coordinate, a source Face-ID, and its own
  properties, such as a future cluster label. Sample count is independent of
  mesh vertex count and face count; a face can contain zero, one, or many samples.
- The mesh entity receives an additional surface-sample component. Existing
  mesh geometry and properties remain authoritative and are not replaced by a
  standalone point-cloud entity.
- **Confirmed storage decision:** samples belonging to a face are contiguous
  in the 2D point cloud. The operator corrected the voice transcription from
  "discontinuous" to "continuous". Do not substitute a linked list or a
  scattered sample array plus a per-face indirection list.
- Each face records the beginning and end of its samples in that attached
  point cloud. Use integer offsets with a half-open `[begin, end)` range;
  `begin == end` represents an empty face. These are logical references to the
  component's storage, not persistent memory addresses.
- Representation default: float `glm::vec2` stores barycentric weights
  `(u, v)` relative to a documented triangle vertex order, with weights
  `(1-u-v, u, v)`. The 3D point is derived from the source triangle. These
  coordinates are local to each triangle and are not a global UV atlas.

## Existing owners and implementation boundary

- Reuse `Geometry::PropertySet` and its revision/mutation semantics for sample
  properties. `Geometry.PointCloud::Cloud` currently requires a 3D position;
  do not pad local coordinates into a fake 3D position or generalize all point
  clouds merely to add this representation. Prefer a small geometry-owned data
  record and compiled operations over another service or registry.
- Extend the existing producer in
  `src/geometry/Geometry.Mesh.SurfaceSampling.cpp` where useful. It
  already computes triangle selection and barycentric weights, but currently
  publishes only a 3D cloud with normals. Preserve that sampling policy and
  reuse its implementation while retaining source associations in the new
  representation. `tests/unit/geometry/Test.SurfaceSampling.cpp` owns the
  current determinism, area distribution, and invalid-triangle coverage.
- Geometry owns the sample data, validation, grouping and 3D evaluation; ECS
  owns the attached CPU component and face range properties; runtime owns
  attachment, mesh revision tracking and transactional publication. Lower
  layers must not acquire runtime/ECS ownership dependencies.
- Reuse the canonical geometry-source/property catalog when exposing sample
  properties. Do not classify the samples as mesh vertices or face properties
  with the wrong cardinality. Document any new sample domain in the canonical
  architecture contract and extend its executable proofs with implementation.

## Acceptance criteria

- [ ] Store finite local 2D coordinates and a valid source triangle Face-ID
      per sample, with independent count-matched custom properties.
- [ ] Attach the surface-sample storage as an additional component of the mesh
      entity and expose each face's `[begin, end)` range into that storage.
      Ranges are in bounds, disjoint, and cover every sample exactly once;
      every sample's Face-ID agrees with the containing face range.
- [ ] Build/group samples deterministically and move every sample property
      with its row. Creation, replacement, and any supported edit publish
      sample data and face ranges together; borrowers cannot retain references
      across reallocation or regrouping.
- [ ] Evaluate 3D positions from the source triangle and barycentric coordinates
      without creating mesh vertices or changing topology. Validate triangle
      ordering, finite coordinates, barycentric bounds with a documented
      tolerance, and degenerate/deleted/out-of-range source faces.
- [ ] Reuse the existing CPU surface sampler to produce retained local
      coordinates and face associations, preserving seed determinism and
      area-weighted sampling. Update in-tree callers/tests together if its API
      changes; do not retain obsolete APIs solely for compatibility.
- [ ] Bind samples to the source mesh identity and topology revision. Position
      edits re-evaluate derived 3D positions; topology replacement, face
      deletion/compaction, or mesh replacement must explicitly invalidate or
      correctly remap the association. Never reinterpret an old Face-ID as a
      different triangle. Reject stale publication and evaluation.
- [ ] Preserve the component, local coordinates, Face-IDs, sample properties
      and face ranges through the current scene save/load path, validating the
      association on load. Persist no pointers or transient runtime handles.
- [ ] Add focused geometry and ECS/runtime contracts for multiple samples per
      face, empty faces, edge/vertex samples, unequal face populations,
      regrouping/property alignment, round-tripping, and source invalidation.
- [ ] Update `docs/architecture/geometry-api-style.md`, relevant ECS/runtime
      ownership documentation, and the contract catalog/proofs as needed for
      the new representation. Refresh the module inventory for changed surfaces.

## Relationship to K-Means

[RUNTIME-211](../runtime/RUNTIME-211-kmeans-property-domain-integration.md) and
[UI-043](../ui/UI-043-kmeans-property-domain-panel.md) currently cover existing
element domains. Their generalization alone does not create this sample storage.
This task establishes the representation; connecting K-Means to it needs an
explicitly scoped follow-up after the metric decision below. It does not block
those tasks' existing domain extensions.

The operator has not yet chosen between Euclidean distance in evaluated 3D
positions and geodesic distance along the surface. Record that decision before
planning the clustering integration. Never compare local `(u, v)` values from
different triangles as if they shared a global coordinate system. Geodesic
clustering, surface-constrained centroids, new GPU backends and editor sampling
controls are outside this storage task.

## Verification

The implementation must register its new focused cases under `SurfaceSamples`;
retain the existing `SurfaceSampling` producer tests.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SurfaceSampling|SurfaceSamples' -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py --check
```
