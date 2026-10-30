---
id: RUNTIME-345
theme: F
depends_on: [ARCH-020, RUNTIME-344]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: two pieces of math the editor wrote out itself move to the libraries that own them; an entity's model matrix comes from the ECS transform module (float as before, plus a double-precision form for the operations that build it in double), and the graph Laplacian of property edges from the geometry smoothing module, shared by the eigenbasis and the harmonic field; results are unchanged except that the harmonic field's Laplacian diagonal is now summed in edge order (rounding only); no catalog contract covers these helpers
---
# RUNTIME-345 — Editor math to the modules that own it (moonshot M2, step 2a)

> **Retired 2026-10-30** at `CPUContracted`. Commit reference: `7619165c8`. Completed. Codex code review: revise; its delta review resolved Blocking 1 and the optional point and Blocking 2 only partly (the limitation is stated in the code comment); no explicit approval is recorded.

## Operator questions (verbatim)
3. (2026-10-27) "start improving the engine by implementing the Moonshot.md. Keep properly track of
   where you and your subagents are at so we can continue anytime properly."
23. (2026-10-29) "Yes, please do 1. - 4." (2: the editor's own algorithm pieces into
    `src/geometry`)

## Goal
- The algorithm inventory (read-only agent, spot-checked) found about 520 lines of math in
  `src/runtime/Editor`. Checked item by item, most are small and differ in semantics from the
  library functions the survey paired them with (the three "bounds copies" run over a subset,
  skip non-finite points or take every point; the six neighbour-row loops differ in capacity,
  completeness rule and target), so replacing them would save a few lines each. Two are exact
  duplicates of code a library owns, and this slice moves those:
  - **Model matrix from a transform** (translation x rotation x scale): `ECS::Components::Transform::
    GetMatrix` exists; the same matrix is written out in `Runtime.RegistrationOperations.Processing.cpp`
    (`ModelMatrixFromTransform`, float), `Runtime.GeometryIntegration` `SpatialIndexCache`
    (`EntityMatrix`, float), and in double precision in `Runtime.RegistrationOperations.CoherentPointDrift.cpp`
    and `Runtime.PointSamplingOperations.cpp` (`ModelMatrix`).
  - **Graph Laplacian L = D - W of property edges**: assembled in
    `Runtime.MeshFieldOperations.Eigenbasis.cpp` (~292-311) and inside `Geometry.HarmonicField.cpp`
    (`BuildLaplacian`, not exported), both from `Geometry::Smoothing::PropertyEdge`.
- The remaining editor math stays for a later slice and is listed with paths in `ARCH-020` (O2);
  this slice is step 2a, not the whole of operator step 2. Most of it reports through editor
  status types; the outlier mean distances are plain arithmetic that a geometry helper could take
  as is.

## Plan
1. **ECS**: `Transform::GetMatrixDouble(const Component&)`: the same matrix computed in double
   (rotation from the `dquat`, columns scaled, translation), exactly the form CPD and sampling use
   now. The float copies call `GetMatrix`: for finite transforms numerically equal, entry by entry,
   except the sign of zero (B1: `translate * mat4_cast * scale` adds products, so a `-0.0`
   translation comes out `+0.0`; a non-finite input spreads NaN further than the copies did, and
   non-finite transforms are invalid input to every caller). The double copies call
   `GetMatrixDouble` (sampling keeps its identity for a missing transform at the call site).
2. **Geometry**: `Geometry::Smoothing::BuildGraphLaplacian(count, edges)` returns the
   `Sparse::SparseBuildResult` of L = D - W with each row's degree accumulated in edge order and
   added once (the eigenbasis' assembly). Contract (B2): repeated edges add up; a self-loop cancels;
   entries summing to exactly zero are dropped (tolerance 0); weights as given, signed included (an
   algebraic choice: the callers pass nonnegative weights, the harmonic field validates them and the
   property graph clamps cotangent weights, so rejecting negatives would also be safe; the author's
   first reason, that cotangent weights may be negative here, was wrong); the build is invalid for an
   endpoint outside `count` (checked before indexing the degree) or a non-finite entry such as an
   overflowing degree, and both callers now check `Valid` (the harmonic field fails the solve; the
   code review found it ignored the flag). The eigenbasis calls it; the harmonic field's
   `BuildLaplacian` calls it and keeps its row conversion. The harmonic field's matrix was summed
   from per-edge diagonal triplets merged after an unstable sort; the new triplet list changes that
   order for the diagonal and for repeated off-diagonal pairs: equal up to rounding (stated).
- Not in this slice: the items listed under Goal.

## Tests (predictions as assertions)
- New `ECSTransformHierarchy` case (the existing transform suite): `GetMatrixDouble` equals the
  double formula the copies used and `glm::dmat4(GetMatrix(t))` within float rounding, for a
  rotated, non-uniformly scaled, translated transform; `GetMatrix` equals the copies' float formula
  exactly.
- New geometry unit test (`HarmonicField.GraphLaplacianIsDegreeMinusWeights`): a small weighted
  graph with a repeated edge gives the expected matrix; an endpoint outside the rows is invalid; a
  self-loop cancels; a 1e-300 weight is kept; the degree is summed in edge order (1e16 + 1 + 1 gives
  1e16, the reverse order 1e16 + 2); a negative weight is taken as given; an infinite weight and an
  overflowing degree are invalid. `HarmonicField.OverflowingDegreeFailsTheSolve`: the solve fails
  instead of returning 1 for row 0 (red before the check).
- The ECS test also pins the one float difference: a `-0.0` translation is `+0.0` in `GetMatrix`,
  and keeps its sign in `GetMatrixDouble`.
- Refactoring: the registration (ICP, CPD), point sampling, spatial index, eigenbasis and
  harmonic field suites pass before and after.
- Mutations, each must turn a test red: `GetMatrixDouble` without the scale; `GetMatrixDouble` as
  `dmat4(GetMatrix(t))` (O1: the double form is its point, caught by `ECSTransformHierarchy`); the
  Laplacian without the diagonal; the Laplacian with +w off-diagonals; the harmonic field ignoring
  `Valid` again.

## Acceptance criteria
- [x] Four model-matrix copies replaced by the ECS functions; two Laplacian assemblies by one.
- [x] Unit tests for both; affected suites green.
- [x] Codex plan and code review; structural checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicGeometryTests IntrinsicECSTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 300 -R '(Registration|CoherentPointDrift|PointSampling|SpatialIndex|Eigenbasis|LaplacianEigen|HarmonicField|ECSTransform)'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Log
- 2026-10-29: Codex plan review (`plan345.answer.md` in this session's scratchpad): revise. B1 the
  "bitwise" claim was false (negative zero, NaN spread): reworded, the `-0.0` case pinned by the
  test. B2 the Laplacian's contract stated and tested (endpoints, self-loop, tiny weight, summation
  order); its suggestion to reject negative weights not taken, since the eigenbasis' cotangent
  weights may be negative (stated in Plan 2). O1 the double-from-float mutation added; the test is
  in the `HarmonicField` suite, which the verification covers. O2 the deferred list moved to
  `ARCH-020` with paths.
- 2026-10-29: Implemented: `GetMatrixDouble` (ECS); `BuildGraphLaplacian` (defined in
  `Geometry.PropertySmoothing.cpp`, the smoothing module's unit with `BuildPropertyNeighborhood`);
  four matrix copies and two Laplacian assemblies replaced. Mutations, each alone and restored,
  all red: `GetMatrixDouble` without the scale (ECS test); `GetMatrixDouble` from the float matrix
  (ECS test); the Laplacian without its diagonal (geometry test and 21 operation tests); +w
  off-diagonals (19 operation tests). Affected suites `Registration|CoherentPointDrift|
  PointSampling|SpatialIndex|Eigenbasis|LaplacianEigen|HarmonicField|ECSTransform|SpectralModes`
  247/247 (before the extra cases).
  After the extra cases and formatting: the affected suites 247/247; `SourceRules` 8/8; layering
  clean; changed lines clang-format 20.1.8. Lines: `src/runtime/Editor` 52,088 -> 52,047, `src`
  -17 overall.
- 2026-10-29: Codex code review (`review345.diff` in this session's scratchpad): revise. Blocking:
  the harmonic field discarded the shared Laplacian's `Valid`, so an overflowing degree (dropped by
  the builder) gave a successful wrong solve; it now fails, with a regression test. Blocking: the
  contract comment and the negative-weight reason were wrong (zeros are dropped, non-finite entries
  make the build invalid, cotangent weights are clamped); both corrected and the behaviour pinned.
  Optional: the ECS test's comment no longer says bit for bit and checks the positive zero; the note
  names the right suite.
  After the fixes: the harmonic field ignoring `Valid` again turns `OverflowingDegreeFailsTheSolve`
  red (mutation, restored); affected suites 248/248.
- 2026-10-29: Codex delta review (`review345b.answer.md`): Blocking 1 and the Optional resolved;
  Blocking 2 partly: the non-finite guarantee holds for entries as added, not after repeated
  signed pairs merge (a counterexample with finite degrees); the comment now says so. A test
  comment claimed the unchecked solve returned 1 (it returned 0 successfully); corrected. Both
  are comment changes; no code changed after this review.
