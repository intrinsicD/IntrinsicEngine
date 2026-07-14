---
id: BUG-029
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-029 — Ray/AABB slab tests NaN-poisoned for axis-parallel rays on slab planes; ray/sphere raycast emits NaN normal

## Goal

- The analytic `Ray`-vs-`AABB` overlap/raycast kernels return correct hit/miss and entry distances for axis-parallel rays whose origin lies exactly on a slab plane, and `RayCast(Ray, Sphere)` never returns a NaN normal — so KD-tree/BVH/Octree ray traversal stops silently pruning subtrees on grid-aligned content.

## Non-goals

- No changes to the watertight ray-triangle kernel (already validates rays) or to GJK/EPA contact paths (normal conventions are owned by the closed BUG-025).
- No SIMD/optimized variants in this task — fix and pin the analytic reference first (method policy: reference is canonical truth).
- No API changes to `Geometry::TestOverlap` / `Geometry::RayCast` signatures or dispatch.

## Context

- Symptom: both analytic slab implementations compute `glm::vec3 invDir = 1.0f / r.Direction;` with no zero-component handling — `Overlap_Analytic(const Ray&, const AABB&)` at src/geometry/Geometry.Overlap.cpp:168-181 and `RayCast_Analytic(const Ray&, const AABB&)` at src/geometry/Geometry.ContactManifold.cpp:116-143. IEEE-754 infinity arithmetic makes the standard slab method correct for most axis-parallel rays, **except** when an origin component equals a slab bound: `(Min − O) * ±inf = 0 * inf = NaN`. `glm::min/max` are comparison-based and NaN-unsafe, so the NaN propagates into `tmin`/`tmax`.
- Concrete walkthrough (false miss): ray `O=(0,5,0)`, `D=(0,−1,0)` vs `AABB{Min=(0,0,0), Max=(1,1,1)}` — the ray pierces the box's top face along its edge plane. `invDir.x = +inf`; `t0s.x = (0−0)·inf = NaN`; `tsmaller.x = NaN`; `tmin = NaN`. Overlap returns `tmax >= tmin && tmax >= 0` → `finite >= NaN` is false → **no overlap reported**. RayCast's `if (tmax < tmin …)` (128) is also NaN-false, then `Distance = tmin > 0 ? tmin : tmax` (131) silently selects the **exit** distance `tmax`, reporting a hit at the wrong point.
- Trigger realism: axis-parallel rays with coordinates exactly on box bounds are common, not exotic — grid-aligned/origin-anchored content plus axis-aligned camera rays (the top-down controller mode from BUG-020, orthographic picking from BUG-026's fallback) and AABBs built from geometry that sits at integer coordinates.
- Blast radius: `TestOverlap(node.Aabb, ray)` gates traversal in `QueryRay` for the KD-tree (src/geometry/Geometry.KDTree.cppm:75), BVH (src/geometry/Geometry.BVH.cppm:70), and Octree — a NaN false-miss at an internal node prunes the entire subtree, so spatial ray queries return empty/incomplete results with no diagnostic. `RayCast(Ray, AABB)` consumers receive exit-point hits. These are public geometry-layer queries available to methods/benchmarks.
- Related defect, same family: `RayCast_Analytic(const Ray&, const Sphere&)` at src/geometry/Geometry.ContactManifold.cpp:95-114 — for a ray starting at (or numerically near) the sphere center, `t` clamps to 0 (107), `hit.Point == s.Center`, and `hit.Normal = glm::normalize(0-vector)` (112) → NaN normal propagates to the caller.
- Asymmetry note for the fix: `Overlap_Analytic` and `RayCast_Analytic` duplicate the slab logic in two files; fixing one and not the other would let `Overlap(ray, box)` and `RayCast(ray, box).has_value()` disagree.
- Impact: wrong-result class (false miss / wrong distance / NaN propagation) in foundational geometry queries; silently corrupts every consumer above them.
- Owner/layer: `geometry` only (`Geometry.Overlap` and `Geometry.ContactManifold` implementation units).

## Required changes

- [x] Extract one shared slab-interval helper (file-local duplication is acceptable if a shared internal header is overkill, but the two implementations must be byte-equivalent in logic) that handles zero direction components explicitly: for each axis with `|d| == 0`, miss immediately if the origin component is outside `[Min, Max]`, otherwise leave that axis unconstrained; compute `tmin/tmax` only from constrained axes. (Equivalent NaN-filtering min/max formulations are acceptable; document the chosen scheme and why it is NaN-free, including the `−0.0` direction case.)
- [x] Apply it in `Overlap_Analytic(Ray, AABB)` (Geometry.Overlap.cpp:168-181) and `RayCast_Analytic(Ray, AABB)` (Geometry.ContactManifold.cpp:116-143); keep the existing inside-origin convention (`Distance = tmin > 0 ? tmin : tmax`) but computed from NaN-free intervals.
- [x] `RayCast_Analytic(Ray, Sphere)`: when `length(hit.Point − s.Center)` is below epsilon, set a documented deterministic fallback normal (recommend `−r.Direction`, unit by `Ray` contract) instead of normalizing a zero vector (Geometry.ContactManifold.cpp:112).
- [x] Sweep for sibling copies of the unguarded `1.0f / r.Direction` slab idiom under `src/geometry/` (non-legacy) and fix any further hits with the same helper; record the sweep result in the PR description.

## Tests

- [x] New unit cases (label `unit;geometry`, extend `tests/unit/geometry/Test_Overlap.cpp` coverage area or add `Test.RaySlabDegenerate.cpp` following the `Test.<Name>.cpp` convention):
  - [x] Axis-parallel ray, origin component exactly on `Min` face plane / on `Max` face plane, ray passing through the box → overlap true, raycast hit with entry distance.
  - [x] Axis-parallel ray exactly along a box edge and through a corner → consistent hit policy (document inclusive boundary).
  - [x] Axis-parallel ray on the slab plane of a box it misses (offset in another axis) → false, no NaN.
  - [x] Ray origin inside the box → hit with `Distance == tmax` (pins the inside convention).
  - [x] Degenerate consistency property over a deterministic corpus including axis-parallel/on-boundary rays: `TestOverlap(ray, box) == RayCast(ray, box).has_value()`.
  - [x] Sphere: ray origin at center and just-off-center → finite unit normal (documented fallback), `Distance == 0` at center.
  - [x] KD-tree/BVH `QueryRay` regression: elements in a node whose AABB boundary coincides with an axis-parallel query ray are still returned (pins the traversal-level symptom, not just the kernel).
- [x] Default CPU gate stays green.

## Docs

- [x] Document the boundary/degenerate conventions (inclusive slab boundaries, inside-origin distance, sphere center-origin normal fallback) where the query contracts are described (module interface comments).

## Acceptance criteria

- [x] No NaN can reach `tmin`/`tmax`/`Normal` in the three touched kernels for any finite ray/shape input (including `±0.0` direction components).
- [x] `Overlap` and `RayCast` agree on hit/miss for the regression corpus.
- [x] KD-tree/BVH ray queries return boundary-coincident elements.
- [x] Existing overlap/raycast tests pass unchanged.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Overlap|Raycast|RaySlab' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
```

2026-06-12 results:
- Commit: pending local BUG loop closure commit.
- `cmake --build --preset ci --target IntrinsicTests` passed.
- Focused BUG regression set passed 31/31, including the new ray/AABB overlap, ray/AABB raycast, sphere center-origin normal, and BVH boundary-query cases.
- Default CPU-supported CTest gate passed after the geometry query fixes.

## Forbidden changes

- "Fixing" by rejecting axis-parallel rays or rays on boundaries as invalid input — these are legitimate queries.
- Introducing epsilon-fudged ray origins (shifting the query) instead of handling the interval math correctly.
- Changing `Ray` validation policy engine-wide (the watertight kernel's existing validation stays as-is).
- Touching legacy mirrors under `src/legacy/`.

## Maturity

- Target: `CPUContracted` — pure CPU geometry kernels fully covered by the default gate. No `Operational` follow-up is owed.
