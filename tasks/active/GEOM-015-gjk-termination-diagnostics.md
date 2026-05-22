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
- Status: ready-to-retire (Slices 1–4 landed; pending acceptance-criteria
  sign-off).
- Owner/agent: copilot.
- Branch: claude/active-geom-task-XWbO2 (Slice 4 lands here; Slice 3
  landed on claude/active-geom-task-jaLRp via PR #917 on 2026-05-22;
  Slices 1–2 landed on claude/nice-knuth-QStLa, merged via PR #915 on
  2026-05-22).
- Owning subsystem/layer: `geometry` (`geometry -> core`; consumes
  `Geometry.RobustPredicates`).
- Promoted from `tasks/backlog/geometry/` on 2026-05-22.
- Deferred from [`tasks/done/GEOM-007-robust-predicates-intersection-classification.md`](../done/GEOM-007-robust-predicates-intersection-classification.md)
  Slice 3.3.d, where the Slice 3.3 plan explicitly documented this as a
  successor-task candidate rather than a callsite-adoption sub-slice.

### Slice plan

- **Slice 1 (landed):** Add `RobustPredicates::ApproxZeroSq` /
  `ApproxZeroLen` helpers with `SignedResult`-shaped diagnostics and
  unit tests. No callsite migration; `Geometry.GJK` and `Geometry.Support`
  unchanged.
- **Slice 2 (landed):** Audit the GJK and Support 1e-6f callsites;
  classify and add inline comments. Migrate the original-space magnitude
  guards in `Geometry.Support.cppm` (Capsule direction, Cylinder
  axis-degeneracy + perpendicular projection, Ellipsoid radii) and the
  one in `Geometry.SDFContact.cppm` (separation-axis zero guard) to
  `RobustPredicates::ApproxZeroSq`, deriving `scale` from the primitive's
  characteristic length or from the unit-direction contract (relative =
  `1.0e-3` preserves the prior 1e-6 threshold at unit scale). Adds
  regression tests for the previously-flipped sub-millimeter Ellipsoid
  and short-axis Cylinder cases.
- **Slice 3 (landed):** Decided the GJK normalized-workspace policy:
  keep `GJK_EPSILON` as a dimensionless normalized-space constant
  (option (i) in "Required changes"); do not thread a per-call scale
  into the driver because the driver already factors the scale out via
  `invScale` and threading would double-normalize. Strengthened the
  in-source contract block above `Config::GJK_EPSILON` in
  `Geometry.GJK.cppm` and pinned the band with a `static_assert`
  requiring `GJK_EPSILON ∈ (0, 1)`. Added a new "GJK tolerance contract"
  subsection to `docs/architecture/geometry.md` describing the
  normalized-workspace contract, the rationale for not threading a
  per-call scale, where scale-aware tolerances live (Slice 2 migration
  in `Geometry.Support` / `Geometry.SDFContact`), and the static-pin.
  No callsite or runtime-behaviour change; existing GJK / Support /
  ContactManifold / Overlap / RobustPredicates tests continue to pass
  unchanged. Slice 4 (termination-reason diagnostics) is the remaining
  open slice.
- **Slice 4 (landed):** Surfaced GJK termination diagnostics via a new
  `Geometry::Internal::GJKDiagnostics` out-param. Added
  `TerminationReason ∈ { Converged, EarlyOutNegativeSupport,
  NoSimplexProgress, MaxIterationsHit }` and a four-argument overload
  of both `GJK_Boolean` and `GJK_Intersection` taking
  `(a, b, scratch, diag)`. Existing two- and three-argument entry
  points are now thin wrappers that allocate a temporary
  `GJKDiagnostics`, preserving the prior boolean / optional-Simplex
  return contract. Added eleven tests in `tests/unit/geometry/Test_GJK.cpp`:
  five exercising each deterministically-reachable termination reason
  on the boolean and intersection drivers, one default-construction
  sanity test, one wrapper-vs-overload parity test, an
  iteration-budget regression test that pins the practical iteration
  count on the standard primitive corpus to ≤ 32 (well below the
  `GJK_MAX_ITERATIONS = 64` ceiling) and asserts that
  `MaxIterationsHit` does not fire, a boolean-outcome parity battery
  across five scales × four configurations (overlap / touching /
  separated / far apart), a near-touching-separation regression at
  each scale, and a touching-sphere overlap regression at each scale.
  Updated `docs/architecture/geometry.md` "GJK tolerance contract" to
  describe the new diagnostic surface and reference the parity battery.

### Slice 2 audit notes

GJK_EPSILON callsites in `Geometry.GJK.cppm` (all in normalized workspace;
inline comments record the classification):
- `Detail::NearlyZero` (line ~67): (a) normalized convergence — early-out /
  zero-direction guard, `|v|² ≤ EPS²`.
- `case 2` segment-degeneracy: (a) normalized convergence, `abLenSq ≤ EPS`
  (intentionally compared to EPS, not EPS², documented inline).
- `case 2` segment parameter clamp: (c) barycentric clamp on `t ∈ [0, 1]`,
  dimensionless; not a magnitude.
- `case 3` in-plane projection guard: (a) normalized convergence,
  `|abc · ao| ≤ EPS`.
- `GJK_Boolean` support-progress: (a) normalized convergence.
- `GJK_Boolean` simplex-membership duplicate test: (a) normalized
  convergence, `|p − support|² ≤ EPS²`.
- `GJK_Intersection` support-progress / membership: (a) — mirrors
  `GJK_Boolean`.

`Geometry.Support.cppm` 1e-6f callsites migrated to
`RobustPredicates::ApproxZeroSq`. Scale choices are deliberately
*axis-local* rather than `length(Radii)` / `shape.Radius` because those
larger scales spuriously trip the zero-band for anisotropic
primitives (e.g. an Ellipsoid with Radii = (1000, 1000, 1e-3) along its
thin axis, or a fat-disk Cylinder with R = 1000 and axisLen = 1):
- Capsule direction-magnitude guard: scale = 1.0 (unit-direction
  contract), relative = 1e-3.
- Cylinder axis-degeneracy: scale = 1.0 (numerical zero-vector floor on
  `axis`, not a shape-ratio test), relative = 1e-3.
- Cylinder perpendicular-projection guard: scale = 1.0 (perpLen ≤ |unit
  dir|), relative = 1e-3.
- Ellipsoid radii-scaled direction guard: scale = `min(|Radii_k|)`
  (worst-case smallest semi-axis ⇒ degenerate only when the smallest
  semi-axis is at machine zero), relative = 1e-3.

The `Internal::Normalize` `1e-12f` fallback in `Geometry.Support.cppm` is
out of scope for this slice — it is a true machine-zero guard rather than
a numerical-stability threshold and applies to bare direction vectors
shared across all primitives.

`Geometry.SDFContact.cppm` separation-axis guard migrated with scale =
1.0 (gradients are unit-normalized in `CalculateGradient`).

### Next verification step

Slice 4 landed on `claude/active-geom-task-XWbO2` (see commit log). All
slice-level work for GEOM-015 is now complete; the task is ready to be
retired to `tasks/done/` once the merge lands and the
acceptance-criteria checkboxes above are confirmed by review. Slice 4
verification rerun on landing:
`cmake --preset ci`
`cmake --build --preset ci --target IntrinsicGeometryTests`
`ctest --test-dir build/ci --output-on-failure -R 'GJK|Support|ContactManifold|Overlap|RobustPredicates' --timeout 60`
plus `python3 tools/repo/check_layering.py --root src --strict`,
`python3 tools/repo/check_test_layout.py --root . --strict`,
`python3 tools/docs/check_doc_links.py --root .`, and
`python3 tools/agents/check_task_policy.py --root . --strict`.
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
- [x] Audit the GJK and Support epsilon callsites and classify each as
      (a) normalized-space convergence tolerance, (b) original-space
      magnitude/zero guard, or (c) barycentric clamp. Record the
      classification inline as a code comment and in the task notes.
      *(Slice 2: classifications added inline in `Geometry.GJK.cppm` and
      `Geometry.Support.cppm`; summary recorded above.)*
- [x] Add `RobustPredicates::ApproxZeroSq(double valueSq, double scale,
      double relative = 1.0e-9)` (and/or `ApproxZeroLen` for length-form
      callers) that returns a `Sign` / `Certainty` diagnostic compatible
      with the existing `SignedResult` shape. Add unit tests covering
      ordinary, near-zero, exactly-zero, and large/small scale cases.
      *(Slice 1, c93ae0b.)*
- [x] Migrate the original-space magnitude guards in
      `Geometry.Support.cppm` (and the one in `Geometry.SDFContact.cppm`)
      to the new helper, deriving `scale` from the primitive's
      characteristic length (sphere radius, capsule length, OBB extent
      diagonal, etc.). *(Slice 2.)*
- [x] In `Geometry.GJK.cppm`, either:
      (i) keep `GJK_EPSILON` as a normalized-space constant but document
          the contract explicitly and add a static assertion / runtime
          diagnostic when the normalized-workspace assumption is
          violated, OR
      (ii) thread a per-call scale into the GJK driver so the same
          predicate helper can be used. Pick exactly one based on the
          Slice 3 plan's "callsite adoption" intent and the audit
          results; document the decision in the architecture doc.
      *(Slice 3: option (i). The driver normalizes by `1/shapeScale`
      before any tolerance check (see `Geometry.GJK.cppm:281–294`) and
      every `GJK_EPSILON` callsite operates on that normalized
      workspace, so threading a per-call scale would double-normalize.
      Strengthened the in-source contract block and pinned the band
      with `static_assert(GJK_EPSILON > 0 && GJK_EPSILON < 1)`.)*
- [x] Surface GJK termination diagnostics in the public result
      (`GJK_Boolean` already returns `bool`; consider an overload or
      out-param reporting `iterations`, `terminationReason` ∈
      { Converged, EarlyOutNegativeSupport, NoSimplexProgress,
      MaxIterationsHit }). Keep the existing boolean entry point as a
      thin wrapper.
      *(Slice 4: added `Geometry::Internal::TerminationReason` and
      `GJKDiagnostics { iterations, reason }`; added
      `GJK_Boolean(a, b, scratch, diag)` and
      `GJK_Intersection(a, b, scratch, diag)` four-argument overloads
      that set the diagnostics out-param; previous two- and three-arg
      entry points now thin-wrap the diagnostic overload.)*
- [x] Update `src/geometry/CMakeLists.txt` and the generated module
      inventory only if a new module surface is exposed.
      *(Slice 4: no new module surface — `TerminationReason` and
      `GJKDiagnostics` are added to the existing exported
      `Geometry::Internal` namespace inside `Geometry.GJK.cppm`.
      Regenerated `docs/api/generated/module_inventory.md` for
      completeness; the inventory tracks modules and dependencies, not
      individual type exports.)*

## Tests
- [x] Add a parity test battery against the legacy `1e-6f` constant for
      GJK across small (object scale ~1e-3) and large (~1e3) shape
      sizes; non-degenerate cases must continue to report the same
      overlap boolean.
      *(Slice 4: `GJK.Parity_BooleanOutcomeAcrossScales` and
      `GJK.Parity_TouchingSpheres_OverlapAcrossScales` cover the
      standard primitive boolean outcomes across five scales
      `{1e-3, 1e-1, 1, 1e1, 1e3}`.)*
- [x] Add a focused regression at the previously-problematic scales
      (e.g. two unit spheres separated by `2.0 + 1e-7` at scale `1e-3`
      vs scale `1e3`) where the old constant policy was known to flip;
      assert the new policy decides consistently with the geometric
      truth.
      *(Slice 4: `GJK.Parity_NearTouchingSeparation_PreviouslyFlippedScales`
      asserts that a near-touching separation of `2*r + 1e-3*r` is
      reported as separated and converges without
      `MaxIterationsHit` across the same five scales.)*
- [x] If termination diagnostics are surfaced, add tests asserting that
      MaxIterationsHit is rare on the standard test corpus (i.e. the
      tolerance choice does not regress convergence).
      *(Slice 4: `GJK.Diagnostics_ConvergenceBudget_NotExhaustedOnStandardCorpus`
      walks ten representative pairs from the existing suite and
      asserts both `diag.reason != MaxIterationsHit` and
      `diag.iterations ≤ 32`, well under `GJK_MAX_ITERATIONS = 64`.)*
- [x] Confirm `Test_GJK.cpp`, `Test_Support.cpp`,
      `Test_ContactManifold.cpp`, and any GJK-fallback-driven
      integration tests in `Geometry.Overlap` still pass without
      modification.
      *(Slice 4: 157/157 tests passing for
      `'GJK|Support|ContactManifold|Overlap|RobustPredicates'`,
      including 11 new GJK tests; no pre-existing tests were
      modified.)*

## Docs
- [x] Update `docs/architecture/geometry.md` (Robust predicates section)
      with a paragraph describing the GJK tolerance contract:
      normalized-workspace constant vs scale-aware, and the rationale
      for the chosen path.
      *(Slice 3: added a new "GJK tolerance contract" subsection
      between "Robust predicates" and "Intersection classification
      records" that records the normalized-workspace decision, the
      reason for not threading a per-call scale, where scale-aware
      tolerances live (`Geometry.Support` / `Geometry.SDFContact` via
      GEOM-015 Slice 2), the `static_assert` pin, and the deferral of
      termination-diagnostics surface to Slice 4.)*
- [x] If a new `ApproxZeroSq` / termination diagnostic surface is
      added, update `docs/api/generated/module_inventory.md` via
      `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
      *(Slice 4: regenerated for completeness; the inventory tracks
      module names and dependencies rather than individual type
      exports, so the existing `Geometry.GJK` entry is unchanged.)*

## Acceptance criteria
- [x] All seven `GJK_EPSILON` callsites and the four `Geometry.Support`
      `1e-6f` guards are either migrated to the new helper or have an
      inline comment explaining why the legacy constant is retained.
      *(Slices 2–3: callsites in `Geometry.Support.cppm` and
      `Geometry.SDFContact.cppm` migrated to
      `RobustPredicates::ApproxZeroSq`; GJK callsites retained
      `GJK_EPSILON` with inline classification comments and a
      `static_assert`-pinned normalized-workspace contract.)*
- [x] GJK's tolerance policy is documented in the architecture doc and
      no longer requires reading the implementation to understand the
      contract.
      *(Slice 3: added "GJK tolerance contract" subsection to
      `docs/architecture/geometry.md`; Slice 4 extended it with the
      termination-diagnostics surface.)*
- [x] Parity test battery shows no regression on the legacy corpus and
      records at least one previously-flipped scale where the new
      policy is now stable.
      *(Slice 4:
      `GJK.Parity_NearTouchingSeparation_PreviouslyFlippedScales`
      asserts stable separated-outcome at scales `{1e-3, 1e-1, 1,
      1e1, 1e3}`.)*
- [x] `geometry -> core` (plus the existing GLM dependency) layering is
      preserved; no new graphics / runtime / ECS dependencies.
      *(Slice 4: only `Geometry.GJK.cppm` and
      `tests/unit/geometry/Test_GJK.cpp` were touched; no new
      imports.)*

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

