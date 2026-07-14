---
id: BUG-025
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-025 — Geometry contact manifold normals violate the documented A→B convention

## Goal

- Make every `Geometry::ComputeContact(a, b)` path return a manifold whose `Normal` points from `a` to `b`, as the convention block at the top of `Geometry.ContactManifold.cppm` documents.

## Non-goals

- No new contact pairs or shape support.
- No physics-layer behavior change: `Extrinsic.Physics.World` already enforces the A→B orientation locally on its `ContactRecord` (PHYSICS-003) and must keep doing so as defense in depth.
- No GJK/EPA algorithm redesign beyond the normal orientation fix.

## Context

- Found during `PHYSICS-003` (2026-06-10): a dynamic sphere resting above a static box produced a contact normal of `(0, +1, 0)` — pointing from the box toward the sphere (B→A) — where the documented convention requires A→B (`(0, -1, 0)` with the sphere as `a`). The physics solver initially pushed the sphere *through* the floor.
- Evidence:
  - `Geometry.ContactManifold.cppm` documents "Normal points from A to B" and the sphere-sphere analytic path follows it (`diff = b.Center - a.Center`).
  - `Geometry.ContactManifold.cpp::Contact_Analytic(Sphere, AABB)` computes `diff = s.Center - closest`, i.e. a B→A normal for the sphere-as-A call.
  - The GJK/EPA fallback (`Contact_Fallback`) returned a B→A-oriented normal for the sphere-above-OBB probe during PHYSICS-003 diagnosis (probe output: `normal=(0,1,0)`, sphere above, box below).
  - The reversed-dispatch branch in `ComputeContact` negates normals for swapped analytic overloads, which is only correct if the underlying overloads honor the documented convention — fixing the leaves fixes the dispatcher.
- Physics-side mitigation (keep): `src/physics/Physics.World.cpp::AppendContactIfPresent` orients the record normal against the shape-center offset, with regression coverage in `PhysicsWorld.ContactRecordNormalFollowsAToBConvention`.
- Owning layer: `geometry` (kernel correctness); consumers are physics and any future geometry queries.
- Fix landed (2026-06-10): `EPA_Solver` now returns `Normal = searchDir` (the closest-face outward normal of the A−B Minkowski polytope is the A→B direction; the previous negation was the inversion), `Contact_Fallback` derives `ContactPointB = ContactPointA - Normal * Depth` (same world point as before, consistent with the corrected normal), and `Contact_Analytic(Sphere, AABB)` computes `diff = closest - s.Center` plus an inverted deep-penetration escape-axis normal so both branches are A→B. The physics-layer guard is unchanged and now acts purely as defense in depth.
- Completed: 2026-06-10.
- PR/commit: branch `claude/pensive-albattani-pu2t14` (pending local commit).

## Required changes

- [x] Audit every `Contact_Analytic` overload and the EPA fallback for normal orientation; fix the sphere-AABB analytic path and any EPA support-direction inversion so `ComputeContact(a, b)` is A→B for all pairs.
- [x] Add geometry unit tests that pin the convention per analytic overload and through the GJK/EPA fallback (sphere/capsule/OBB pairings, both argument orders).
- [x] Keep the physics-layer orientation guard and its regression test in place (defense in depth; do not remove).

## Tests

- [x] New `unit;geometry` tests for manifold normal direction across all supported pairs and argument orders.
- [x] Existing physics tests (`ctest -L physics`) stay green, including `PhysicsWorld.ContactRecordNormalFollowsAToBConvention`.

## Docs

- [x] Update the convention comment block in `Geometry.ContactManifold.cppm` only if behavior intentionally diverges (it should not).
- [x] Note the fix in `docs/architecture/physics.md` (remove the BUG-025 caveat) when closing.

## Acceptance criteria

- [x] All `ComputeContact` paths return A→B normals, proven by per-pair unit tests.
- [x] Physics tests pass unchanged.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicPhysicsWorldTests
ctest --test-dir build/ci --output-on-failure -L 'geometry' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -L 'physics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes

- Do not remove the physics-layer A→B orientation guard.
- Do not change the documented convention to match the buggy behavior.
- Do not mix this kernel fix with new collision features.

