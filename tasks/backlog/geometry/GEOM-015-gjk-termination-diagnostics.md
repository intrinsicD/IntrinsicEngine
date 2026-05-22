# GEOM-015 — GJK termination diagnostics and scale-aware tolerance policy

## Goal
- Replace `Geometry::GJK::Config::GJK_EPSILON = 1e-6f` (and the scattered
  `1e-6f` magnitude guards in `Geometry.Support`) with a scale-aware,
  diagnostics-bearing tolerance policy that integrates with the
  `Geometry.RobustPredicates` foundation laid by GEOM-007.

## Non-goals
- Not a rewrite of the GJK algorithm itself; the existing iteration,
  simplex evolution, and direction-update structure stay as-is.
- No EPA / contact-manifold generation changes; this task only touches
  termination/degeneracy tolerances.
- No public API expansion of `Geometry.GJK` beyond what is required to
  surface termination diagnostics (early-out reason, iteration count).
- No new RobustPredicates surface beyond a single `ApproxZeroSq` /
  `ApproxZeroLen` helper unless a second callsite genuinely needs it.
- No paper-method-workflow work — Shewchuk-style adaptive predicates
  remain on the GEOM-007 Slice 4 / its own successor task.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core`; consumes
  `Geometry.RobustPredicates`).
- Deferred from [`tasks/active/GEOM-007-robust-predicates-intersection-classification.md`](../../active/GEOM-007-robust-predicates-intersection-classification.md)
  Slice 3.3.d, where the Slice 3.3 plan explicitly documented this as a
  successor-task candidate rather than a callsite-adoption sub-slice.
- Current state (as of GEOM-007 Slice 3.3.c landing):
  - `src/geometry/Geometry.GJK.cppm` defines `Config::GJK_EPSILON = 1e-6f`
    and uses it at 7 sites covering early-out direction, support-progress
    test, simplex-membership duplicate test, segment-degeneracy guard,
    barycentric clamp tolerance, and the in-plane projection guard.
  - `src/geometry/Geometry.Support.cppm` has three local `1e-6f` magnitude
    guards (`lenSq < 1e-6f`, `axisLen2 > 1e-6f`, `perpLen2 > 1e-6f`,
    `len2 < 1e-6f`) acting as zero-vector guards in support primitives.
  - `src/geometry/Geometry.SDFContact.cppm` has a `1e-6f` separation-axis
    guard at line ~66.
- Important design subtlety: `GJK_Boolean` and `GJK_Intersection`
  already normalize their workspace by `1.0 / shapeScale` (see comments
  at `Geometry.GJK.cppm:252–261`). The `1e-6f` tolerance is therefore
  *intentionally in normalized space* and is not the same kind of bug
  the GEOM-007 frustum migration fixed. Scale-aware policy in GJK proper
  may amount to documenting the normalized-space contract more clearly
  rather than introducing per-call scale arguments. The
  `Geometry.Support` and `Geometry.SDFContact` guards work in original
  shape space and *do* benefit from `RobustPredicates::ScaledEpsilon`
  derived from the local primitive's characteristic length.
- Downstream consumers that must continue to pass:
  - `tests/unit/geometry/Test_GJK.cpp`,
  - `tests/unit/geometry/Test_Support.cpp`,
  - `tests/unit/geometry/Test_ContactManifold.cpp`,
  - any contact / collision integration tests under `tests/integration/`
    that exercise overlapping shapes via the GJK fallback path in
    `Geometry.Overlap` (e.g. unhandled `Overlap_Analytic` pairs that
    fall through to `GJK_Boolean`).

## Required changes
- [ ] Audit the GJK and Support epsilon callsites and classify each as
      (a) normalized-space convergence tolerance, (b) original-space
      magnitude/zero guard, or (c) barycentric clamp. Record the
      classification inline as a code comment and in the task notes.
- [ ] Add `RobustPredicates::ApproxZeroSq(double valueSq, double scale,
      double relative = 1.0e-9)` (and/or `ApproxZeroLen` for length-form
      callers) that returns a `Sign` / `Certainty` diagnostic compatible
      with the existing `SignedResult` shape. Add unit tests covering
      ordinary, near-zero, exactly-zero, and large/small scale cases.
- [ ] Migrate the original-space magnitude guards in
      `Geometry.Support.cppm` (and the one in `Geometry.SDFContact.cppm`)
      to the new helper, deriving `scale` from the primitive's
      characteristic length (sphere radius, capsule length, OBB extent
      diagonal, etc.).
- [ ] In `Geometry.GJK.cppm`, either:
      (i) keep `GJK_EPSILON` as a normalized-space constant but document
          the contract explicitly and add a static assertion / runtime
          diagnostic when the normalized-workspace assumption is
          violated, OR
      (ii) thread a per-call scale into the GJK driver so the same
          predicate helper can be used. Pick exactly one based on the
          Slice 3 plan's "callsite adoption" intent and the audit
          results; document the decision in the architecture doc.
- [ ] Surface GJK termination diagnostics in the public result
      (`GJK_Boolean` already returns `bool`; consider an overload or
      out-param reporting `iterations`, `terminationReason` ∈
      { Converged, EarlyOutNegativeSupport, NoSimplexProgress,
      MaxIterationsHit }). Keep the existing boolean entry point as a
      thin wrapper.
- [ ] Update `src/geometry/CMakeLists.txt` and the generated module
      inventory only if a new module surface is exposed.

## Tests
- [ ] Add a parity test battery against the legacy `1e-6f` constant for
      GJK across small (object scale ~1e-3) and large (~1e3) shape
      sizes; non-degenerate cases must continue to report the same
      overlap boolean.
- [ ] Add a focused regression at the previously-problematic scales
      (e.g. two unit spheres separated by `2.0 + 1e-7` at scale `1e-3`
      vs scale `1e3`) where the old constant policy was known to flip;
      assert the new policy decides consistently with the geometric
      truth.
- [ ] If termination diagnostics are surfaced, add tests asserting that
      MaxIterationsHit is rare on the standard test corpus (i.e. the
      tolerance choice does not regress convergence).
- [ ] Confirm `Test_GJK.cpp`, `Test_Support.cpp`,
      `Test_ContactManifold.cpp`, and any GJK-fallback-driven
      integration tests in `Geometry.Overlap` still pass without
      modification.

## Docs
- [ ] Update `docs/architecture/geometry.md` (Robust predicates section)
      with a paragraph describing the GJK tolerance contract:
      normalized-workspace constant vs scale-aware, and the rationale
      for the chosen path.
- [ ] If a new `ApproxZeroSq` / termination diagnostic surface is
      added, update `docs/api/generated/module_inventory.md` via
      `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] All seven `GJK_EPSILON` callsites and the four `Geometry.Support`
      `1e-6f` guards are either migrated to the new helper or have an
      inline comment explaining why the legacy constant is retained.
- [ ] GJK's tolerance policy is documented in the architecture doc and
      no longer requires reading the implementation to understand the
      contract.
- [ ] Parity test battery shows no regression on the legacy corpus and
      records at least one previously-flipped scale where the new
      policy is now stable.
- [ ] `geometry -> core` (plus the existing GLM dependency) layering is
      preserved; no new graphics / runtime / ECS dependencies.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure \
      -R 'GJK|Support|ContactManifold|Overlap|RobustPredicates' \
      --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not introduce Shewchuk-style adaptive predicates here — that work
  belongs to GEOM-007 Slice 4 or its successor.
- Do not change `GJK_Boolean` / `GJK_Intersection` return types in a
  breaking way; add overloads or out-params for diagnostics.
- Do not rewrite the simplex evolution / direction update algorithm.
- Do not couple this task to EPA / contact manifold generation work.
- Do not mix mechanical file moves with semantic refactors.

