# METHOD-041 inspection record

The delivered slice is a CPU reference and standalone curve inspector. It does
not change METHOD-040 partitions or Sandbox behavior. The
[formulation](curvature_extrema.md) fixes the numerical choices;
[UI-053](../../../tasks/active/UI-053-curvature-extremum-engine-overlay.md)
owns engine/config/overlay integration. Part-boundary selection and merging
remain a later experiment after inspecting these candidates.

## Executed checks

The final Clang 23 `ci` run passes 39 selected curvature/partition cases,
including all ten new extremum cases. Separate `ci-asan` and `ci-ubsan` runs
pass both test producers: 48 existing feature/partition cases and ten extremum
cases in each sanitizer. These are focused CPU checks, not a full-engine or
Vulkan verification. Raw logs and per-case sanitizer XML are in
[`tasks/evidence/METHOD-041`](../../../tasks/evidence/METHOD-041).

The analytic fixtures check the known central crease of a Gaussian extrusion,
orientation reversal, retriangulation/refinement, scale/translation, bounded
noise, constant-curvature negatives, exact sharp-fold separation, unchanged
source properties, and explicit invalid/work-limit results. Fourteen Python
checks reject corrupted interpolation and curve graphs. Nine native CLI probes
exercise configuration failures and refusal to overwrite an existing source.
The existing viewer's validation tests and test-routing regressions also pass.

The isolated local browser exercised all 41 available views across frog,
sculpt, trim-star, fandisk, dolphin and bumpy_torus, with no WebGL error. It
checked confidence, strength and scale-agreement filtering. Local screenshots
of frog, sculpt and trim-star were visually inspected by the implementing
agent. This is rendering/interaction evidence, not operator approval of curve
locations or a semantic-part quality score.

## Local exploratory cohort

The six read-only OBJ inputs came from the operator's local dataset directory.
Native exports bind input/output hashes, original triangles and subtriangle
interpolation. The final artifact is `build/method041-inspection/index.html`;
its adjacent cohort and native exports remain local, including the previous
METHOD-040 comparison where identical geometry is available. There is no
METHOD-040 comparison for trim-star in the retained comparison cohort.

[Six schema-v2 measurements](../../../tasks/evidence/METHOD-041/measurements)
pass the canonical validator. They use the frozen defaults and CPU reference
implementation. Source state is explicitly dirty and `claim_eligible: false`:
one cold extraction per mesh is an exploratory profile, with no comparable
performance baseline or speedup claim. The earlier Debug and intermediate
sharp-barrier inspections remain separate local artifacts; their timings must
not be mixed with the final Release run.

The [cohort summary](../../../tasks/evidence/METHOD-041/cohort-summary.json)
records support counts. On frog the finest radius supports substantially fewer
vertices than the middle radius. This motivates showing sampling support beside
the curves; it does not establish why any particular desired part seam is
missing. Smooth gaps near hard edges are deliberate because hard-edge endpoints
block smooth support. The viewer distinguishes these exact edges from smooth
principal and mean-curvature candidates.

## Review findings and fixes

- The exact sharp-fold control initially exposed parallel smooth responses near
  a hard crease. Blocking smooth neighborhoods at sharp/boundary vertices leaves
  the source sharp edge and makes the strengthened fold control pass.
- Dropping float-zero-length output segments exposed a winding-reversal mismatch
  near mesh vertices. Coalescing intersections within the documented publication
  precision restores the original orientation assertion; no tolerance was widened.
- Adding the extremum fixtures to the previous pure test producer exceeded its
  fixed 120-second ASan timeout. The new producer keeps all assertions and the
  same timeout; both final groups pass. The initial
  [timeout log](../../../tasks/evidence/METHOD-041/asan-group-timeout.log) remains
  available. Routing and replacement-only grouped registration include the new
  producer. The [whole-registry comparison](../../../tasks/evidence/METHOD-041/grouped-registration.json) confirms identical logical cases in individual and grouped plans after refreshing the stale ASan runtime contract executable.
- Export validation independently recomputes connected components, endpoint and
  junction counts, lengths, signal/scale identities and source-face incidence.
  Browser checks found and fixed a `Set.size` call error and stale inspection
  counters when switching back to a partition view.

Claude's [bounded public-mathematics critique](../../../tasks/evidence/METHOD-041/claude-math-review.json)
raised sign ambiguity, unreliable umbilic directions and derivative fitting
bias. These concerns inform the line-field rejection and analytic controls.
The critique did not inspect repository source or private meshes. Two proposed
requirements were not adopted: sign-covariant per-vertex filtering followed by
consistent face alignment is valid, and fitting a constrained symmetric operator
is valid least squares; symmetry need not be imposed only after fitting.
These decisions were checked against the primary formulations cited in the
method note, rather than treating the review as an approval certificate.

## Scope and architecture review

One geometry module owns value parameters, detached results and local scratch
storage. Its only module dependencies are geometry-owned mesh/curvature APIs.
The native runner and analytic tests consume the public API. There is no new
service, mutable global state, engine dependency, GPU backend or compatibility
shim. Runner JSON is the current serializable control surface; UI-053 owns the
shared engine preview/apply lane before Sandbox adoption.

| Clean-workshop row | Disposition |
| --- | --- |
| 1. Layer imports | Pass: strict layering; geometry-only imports. |
| 2. CMake links | Pass: existing core/geometry/JSON dependencies. |
| 3. Public types | Pass: no higher-layer or backend types exported. |
| 4. Renderer ownership | N/A: no engine renderer change. |
| 5. Typed frame passes | N/A: no frame-graph change. |
| 6. Recipe dependencies | N/A: no recipe change. |
| 7. Maturity follow-up | CPU reference/standalone inspection only; UI-053 owns engine integration. |
| 8. Temporary exceptions | N/A: none introduced. |

Source documentation audit has no objective errors. The new declaration comment
states mandatory sign/failure/ownership contracts. The existing method README's
length/history findings are outside this implementation; its addition is a short
current-state link. Module inventory, task policy, manifests, documentation links,
test layout, root hygiene and the workshop checks were verified.

## Reproduction

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryCurvatureExtremaTests IntrinsicGeometryFeaturePartitionTests IntrinsicCurvatureExtremaMesh
ctest --test-dir build/ci --output-on-failure -R 'CurvatureExtrema|CurvatureBoundaryGraph|CurvatureBoundaryPartition|CurveCoverageV1Profile' --timeout 120
cmake --preset ci-asan -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-asan --target IntrinsicGeometryCurvatureExtremaTests IntrinsicGeometryFeaturePartitionTests
ctest --test-dir build/ci-asan --output-on-failure -R '^IntrinsicGeometry(CurvatureExtrema|FeaturePartition)Tests.Grouped$' --no-tests=error --parallel 1
cmake --preset ci-ubsan -DINTRINSIC_GROUP_PURE_CTEST=ON
cmake --build --preset ci-ubsan --target IntrinsicGeometryCurvatureExtremaTests IntrinsicGeometryFeaturePartitionTests
ctest --test-dir build/ci-ubsan --output-on-failure -R '^IntrinsicGeometry(CurvatureExtrema|FeaturePartition)Tests.Grouped$' --no-tests=error --parallel 1
python3 tests/regression/tooling/Test.CurvatureExtremaViewer.py
python3 tests/regression/tooling/Test.CurvatureBoundaryViewer.py
python3 tools/benchmark/validate_benchmark_results.py --root tasks/evidence/METHOD-041/measurements --manifests-root benchmarks --strict
```
