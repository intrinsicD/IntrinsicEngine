---
id: PHYSICS-008
theme: C
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Operator-requested backlog record; implementation slices owe their own tests and benchmark evidence for any speed claim.
contract_schema: 1
contracts: []
contract_review: Acceleration of the existing PHYSICS-002 collision contract plus a new bounding-volume variant in Geometry.BVH; each slice must preserve deterministic candidate and contact output, so no new integration contract is declared yet.
---
# PHYSICS-008 — Spatial broadphase and k-DOP hierarchies for deformable collision

## Goal

Replace the all-pairs rigid broadphase with a spatial one, and prepare
collision for deforming geometry (cloth, soft bodies, deforming meshes) with
k-DOP bounding-volume hierarchies that are refitted instead of rebuilt.

## Current state

- `Physics::World::ComputeCollisionContacts` (`src/physics/Physics.World.cpp`)
  enumerates all shape pairs, O(n²), and dispatches narrowphase for every
  pair without a bounds prefilter. [Physics](../../../docs/architecture/physics.md)
  documents all-pairs as the first slice and requires later acceleration to
  keep deterministic candidate order (body slot, then shape index).
- Colliders are sphere, capsule and box/OBB. There are no mesh colliders and
  no tree-versus-tree queries.
- The XPBD cloth reference (PHYSICS-007) supports static half-spaces only:
  sphere colliders are declared but unsupported and there is no
  self-collision. Deformable collision is planned.
- `Geometry.BVH` is the shared binary median-split AABB tree (merged with
  the former KD-tree). It rebuilds from scratch and has no refit.

## Why k-DOPs

Operator decision on 2026-09-26: deformations will come, so bounding volumes
must be cheap to update. k-DOPs (for example 18-DOPs) bound tighter than AABBs
but keep interval tests over fixed directions, so they refit bottom-up as fast
as AABBs. OBBs fit rigid, strongly anisotropic geometry better but are costly
to recompute per step; they stay out of scope unless a rigid mesh-versus-mesh
consumer appears.

## Slices

1. **Rigid broadphase.** Sweep-and-prune or a `Geometry.BVH` over world-space
   shape bounds, with an AABB prefilter before narrowphase. Sort candidates
   into the documented deterministic order; contacts must match all-pairs
   output exactly on existing fixtures.
2. **Refit.** Bottom-up bounds refit for `Geometry.BVH` after element motion,
   with a documented rebuild trigger (for example bound-volume growth), and
   tests that refit and rebuild give identical query results.
3. **k-DOP volumes.** k-DOP node volumes in `Geometry.BVH` as a build/volume
   option (not a separate tree), with overlap and point-distance tests.
4. **Deformable consumer.** Cloth sphere/mesh colliders and self-collision
   candidates for the XPBD reference through the refitted k-DOP tree, compared
   against an exhaustive candidate oracle.

## Acceptance criteria

- [ ] Slice 1: contacts and candidate order equal the all-pairs result on the PHYSICS-002 fixtures; an added many-body fixture measures the scaling.
- [ ] Slice 2: refitted and rebuilt trees return identical overlap/kNN/radius results after deformation sequences.
- [ ] Slice 3: k-DOP overlap tests are conservative (no missed pair versus exact primitive tests) on randomized fixtures.
- [ ] Slice 4: cloth collision candidates equal the exhaustive oracle; unsupported-collider reporting is updated accordingly.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L physics --timeout 60
ctest --test-dir build/ci --output-on-failure -R "BVH|KDTree" --timeout 60
```
