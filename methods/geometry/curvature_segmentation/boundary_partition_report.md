# METHOD-040 overnight comparison — 2026-09-06

The experimental boundary solver distributes frog's surface across several
substantial regions while retaining sculpt's five hard-bounded patches. It is
available through an opt-in CPU runner and local interactive viewer. It remains
an **unadopted candidate**: its feature-boundary objective cannot satisfy the
inherited curvature-only split oracle, and several organic meshes retain tiny
hard-constrained regions. No production method or runtime default changes.

## What was implemented

One private sparse graph kernel, one geometry CPU API, exact tiny-graph checks,
and a native OBJ cohort runner implement the
[separately frozen objectives](boundary_partition_experiment.md).
The final comparison profile, `curves`, uses signed source-edge boundary costs,
connected-region complexity, and explicit small-region agglomeration. Soft
feature attenuation applies around sufficiently long connected hard curves;
short hard facts still require cuts without suppressing nearby soft evidence.

The graph objective is global in its variables. The larger-graph solver is a
bounded heuristic using contraction and split proposals; it does not establish
a global optimum. Area cleanup can increase the preceding optimized energy and
reports that difference explicitly. It never deletes faces or crosses a hard
constraint. A remaining small-region count means no legal adjacent merge in the
current region graph, not impossibility under every feasible repartitioning.

Independent source review caught stale regional merge gains and numerically
unstable weighted-variance increments. Both have direct regressions. Additional
fixes bound stale heap storage and flow work, preserve exact native geometry in
the viewer, and reject unbound or changed output sidecars. No new dependency,
backend registry, runtime operation, or generic optimizer framework was added.

## Final local comparison

These are single Debug CPU observations from a dirty source state, bound by
source-file, executable, input, and output hashes. The source and inputs remained
unchanged during the run. All previously inspected meshes are comparison data,
not held-out validation. Region count and area balance do not establish semantic
parts or general segmentation quality.

The baseline is the existing METHOD-039 local diagnostic with runtime-default
patch complexity 0.5. The final candidate uses the fixed `curves` profile on
every mesh. The full cohort also retains medium and coarse cleanup profiles.
The table's tiny-region count means area below 0.1% of total mesh area; it is
separate from the implementation's normalized-area cleanup threshold.

| Mesh | Faces | Regions, local → curves | Largest area, local → curves | Tiny, curves | Feature recall | Unsupported boundary | Partition ms, curves |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| sculpt | 7,342 | 5 → 5 | 45.6% → 45.6% | 0 | 31.2% | 0.0% | 96.6 |
| frog | 19,106 | 5 → 15 | 99.7% → 26.1% | 1 | 49.9% | 17.8% | 365.1 |
| fandisk | 12,946 | 15 → 18 | 24.5% → 24.5% | 0 | 31.2% | 6.3% | 186.5 |
| dolphin | 5,120 | 11 → 18 | 95.2% → 78.5% | 9 | 56.4% | 26.7% | 78.8 |
| elephant | 49,918 | 27 → 30 | 99.1% → 49.9% | 21 | 30.9% | 25.7% | 1018.7 |
| bumpy_torus | 33,630 | 40 → 30 | 98.1% → 86.8% | 26 | 26.9% | 27.9% | 624.6 |
| genus3 | 13,312 | 8 → 3 | 99.6% → 72.8% | 1 | 54.7% | 39.0% | 236.6 |
| cube | 12,288 | 6 → 6 | 16.7% → 16.7% | 0 | 34.8% | 0.0% | 171.6 |
| saddle2 | 1,352 | 1 → 1 | 100.0% → 100.0% | 0 | 100.0% | 0.0% | 14.8 |

Feature recall is selected length divided by all input hard-or-positive-soft
feature length. Unsupported boundary is selected length with neither input hard
nor positive-soft support, divided by all selected length. Neither is a
standalone quality score: not every detector response deserves a cut, and some
unsupported closure is necessary. Saddle2 has no input features or boundaries;
its recall is the runner's vacuous value 1, not evidence of successful detection.

Frog's five baseline labels include a region holding 99.7% of its surface; the
raw label count therefore overstates the visible segmentation. With `curves`,
four regions exceed 10% each and the largest is 26.1%. Seventeen cleanup merges
remove small fragments, while two sub-threshold regions remain blocked in the
current adjacency graph. One of those is below 0.1% of total surface area.
This is a concrete change in geometric coverage, not evidence that the cuts
match anatomical parts.

Sculpt and cube retain only their hard crease boundaries in this candidate.
Dolphin, elephant, and bumpy_torus remain limited by isolated mandatory facts,
large background regions, or both. Increasing the cleanup radius does not
reliably produce better or nested partitions. The curve-length condition avoids
suppressing useful soft evidence around isolated hard facts; it does not solve
all of those remaining partitioning limitations.

The partition column excludes curvature and feature detection. Total `curves`
execution ranges from about 83 ms on saddle2 to 25.4 seconds on elephant; the
unchanged feature detector accounts for about 23 seconds on elephant. Runs had
no warmup or repetition and overlapped other host work. Different objectives and
these timings do not establish a speedup or a release performance guarantee.

## Input and adoption failures remain visible

All four profiles completed on nine admissible meshes with complete face
assignment and every input hard constraint preserved. Twelve run records from
three other inputs failed, so the overall 48-run cohort command returned 1:

- `bunny10k.obj`: 1,113 unreferenced vertices fail the current strict owning-
  surface preflight; its referenced triangle surface is a separate possible
  explicit extraction experiment, not a silently substituted input.
- `sphere.obj`: 48 triangles have exactly zero area in the file coordinates.
- `office_chair.obj`: repeated polygon indices fail OBJ import, and most faces
  are nontriangular in any case. No repair or triangulation was performed.

The [input audit](../../../tasks/evidence/METHOD-040/experiments/input-rejections.md)
records the counts and exact conditions. Input files remain unchanged in Dropbox.

The weak regional objective returns one region on the immutable unmarked-
curvature closure fixture, giving VI=log(2), above the original 0.01 limit.
Boundary-only profiles cannot split this F=0 case by their chosen objective.
`RegionalCandidateRefutesFrozenUnmarkedClosureGate` retains that rejection as an
executable negative control. A passing negative-control test is not a passing
adoption gate. The thresholds, fixtures, METHOD-039 seed refutation, and source
feature detector remain intact. There is no positive CPUContracted adoption
verdict and no runtime/config/UI follow-up is opened by this result.

Earlier rejected weights, regional work limits, a stale-runner execution, and
misidentified scale-profile results remain in the
[experiment evidence directory](../../../tasks/evidence/METHOD-040/experiments/).
Misidentified outputs are excluded from the validated final comparison.

## Reproduce and inspect locally

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicCurvatureBoundaryMesh IntrinsicGeometryFeaturePartitionTests
build/ci/bin/IntrinsicCurvatureBoundaryMesh \
  /home/alex/Dropbox/Work/Datasets/obj/frog.obj /tmp/frog-boundary curves
python3 benchmarks/runners/curvature_boundary_cohort.py \
  --runner build/ci/bin/IntrinsicCurvatureBoundaryMesh \
  --dataset-root /home/alex/Dropbox/Work/Datasets/obj \
  --output /tmp/method040-new-run --modes local clean_medium clean_coarse curves
python3 benchmarks/runners/curvature_boundary_viewer.py \
  --cohort /tmp/method040-new-run --output build/method040-review/index.html
```

Use a new output directory for each cohort. Its expected invalid-input failures
must be read before continuing to the viewer; do not describe its exit 1 as a
fully passing dataset gate. The viewer includes excluded records and validates
all source/output bindings before displaying a run. It is a standalone local
HTML file with synchronized camera controls, region colors, hard/soft evidence,
and final boundaries. It contains local mesh data and is not committed.

The final local artifact is `build/method040-review/index.html`; raw exports are
under `/tmp/method040-cohort-final`. The committed
[cohort record](../../../tasks/evidence/METHOD-040/experiments/final-cohort.json)
retains all 48 outcomes, and 36 successful raw observations are separately
[recorded as schema-v2 results](../../../tasks/evidence/METHOD-040/benchmarks/final-cohort/)
with `claim_eligible: false`. Their thresholds check assignment, hard barriers,
and bounded health, not segmentation quality.

## Verification and review

- 28 boundary-solver CTest cases pass, including independent exact enumeration,
  hard barriers, cleanup energy accounting, disconnected inputs, numeric extremes,
  work limits, inherited controls, and the explicit negative adoption oracle.
- Ten synthetic viewer regressions pass. A browser review exercised all 36 valid
  mesh/profile combinations, native saddle2 geometry, both overlays, synchronized
  camera controls, and all 12 excluded records without JavaScript/WebGL errors or
  external requests. The later parser-only fixes have direct regressions.
- The final canonical CPU gate passes all 4,295 selected entries, with the
  expected unsanitized GLFW/LSan skip. The first run had one existing asset-import
  failure; 100 isolated repetitions passed. Its independently identified
  completion race remains tracked in
  [BUG-172](../../../tasks/backlog/bugs/BUG-172-synchronous-cpu-load-completion-race.md),
  without proven attribution of that particular intermittent failure.
- The first full ASan gate exposed two fixture-ordering defects, an overloaded
  geometry group, and a synthetic leak-control timeout. The fixture fixes are
  [BUG-173](../../../tasks/done/BUG-173-runtime-test-ordering.md); both pass 20 CPU
  and 20 ASan repetitions each. Identical detector setup is reused across cleanup
  profiles, and a separate required feature-partition producer preserves the
  original 120-second process budget. Real-build parity proves the same 4,293
  GoogleTest cases plus two manual records, with no omissions or duplicates.
  The full UBSan gate passes 2,753 physical entries (the same 4,295 logical
  records), with its expected non-ASan leak-control skip. The final ASan gate
  passes all 2,753 physical entries without skips, including the leak control;
  the three geometry groups take 63.15, 50.94, and 83.92 seconds under their
  unchanged 120-second budgets. The retained final command receipts are
  [CPU](../../../tasks/evidence/METHOD-040/commands/cpu-ctest-serial.json),
  [ASan](../../../tasks/evidence/METHOD-040/commands/asan-ctest-local-symbols.json),
  and [UBSan](../../../tasks/evidence/METHOD-040/commands/ubsan-ctest-serial.json).
  The test-harness-only BUG-174 fix landed during the UBSan run; its non-ASan
  skip is unchanged, and the final ASan run validates the repaired harness.
- Strict routing exposed a missing manual classification for the earlier atlas
  smoke, fixed in [BUG-175](../../../tasks/done/BUG-175-uv-atlas-smoke-test-routing.md).
  Concurrent CTest discovery also corrupted two generated registration files;
  serial regeneration restored exact parity. The coordination gap remains
  [BUG-176](../../../tasks/backlog/bugs/BUG-176-concurrent-ctest-discovery.md).
  [BUG-174](../../../tasks/done/BUG-174-synthetic-lsan-control-timeout.md)
  fixes the synthetic LSan timeout: inherited debuginfod lookup stalled its
  symbolizer. The harness now uses local debug symbols; all twenty complete
  leak/lifetime repetitions pass with unchanged suppressions and time limits.
- Formal task retirement is blocked independently by
  [BUG-171](../../../tasks/backlog/bugs/BUG-171-required-command-receipt-supersession.md):
  required failed development receipts cannot currently be reconciled with later
  successful checks. Every original receipt remains intact.

Claude contributed two bounded public-mathematics reviews through MCP. Automatic
approval rejected sharing repository/data context, so no such payload was sent;
the independent source review was local. A separate rejection prevented viewer
screenshot transfer. Final browser validation is programmatic, and the latest
candidate has no agent visual-quality verdict from those blocked screenshots.


The scoped architecture review keeps all owning state in geometry and borrowed
inputs synchronous; the benchmark consumes only public method APIs. The private
graph header is a present exact-oracle test seam, not a generic service. Automatic
strict clean-workshop checks pass. Manual scorecard:

| Row | Disposition | Evidence |
| --- | --- | --- |
| 1. Promoted layer imports | pass | Strict layering check; geometry remains geometry/core. |
| 2. CMake target links | pass | Existing geometry target; runner links public method owner. |
| 3. Higher-layer API types | pass | New API exposes geometry values and borrowed spans only. |
| 4. Renderer ownership | n/a | No renderer change. |
| 5. Typed pass IDs | n/a | No frame passes. |
| 6. Recipe dependencies | n/a | No recipe change. |
| 7. Maturity closure | n/a | Negative experiment; task not retired or promoted. |
| 8. Temporary exceptions | pass | No new exception or allowlist entry. |

## Subsequent operator inspection

The operator subsequently requested engine/UI access. The separate
[UI-052 diagnostic integration](../../../tasks/active/UI-052-method040-diagnostic-inspection.md)
adds `feature_boundary_curves_v1` to the existing Curvature window using the
same fixed profile as the runner. See the [current runtime instructions](README.md#runtime-and-ui).
The overnight observations, failed oracle, and no-adoption verdict above remain
unchanged; UI access is not a new quality result.
