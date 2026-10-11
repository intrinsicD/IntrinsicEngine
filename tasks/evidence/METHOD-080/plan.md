# METHOD-080 — Invalid-row census in the observed Nyström variants

1. "Bei welchem sampling Verfahren passiert das?"
2. "Kannst du untersuchen welche Zeilen das genau sind?"
3. "Was zeichnet diese Punkte aus?"
4. "An welcher Stelle werden die affinitätssummen negativ?"

Operator-directed diagnosis continues METHOD-079, outside standing Framework24 P0. The shortest
path is the same frozen synthetic fixture and prefixes through the last known failing draw; no new variant comparison,
production remedy, tuning decision or performance claim. Points/fixed/random actually uses
FarthestPoint (default LandmarkSampling.Method, FirstIndex=0), fixed point identities with current
source positions, half from each cloud. Its 'random' box pattern is ignored. Box/resampled/random
uses uniform random points in the union of sigma-padded boxes and changes its seed by draw.

## Small end-to-end slice and mapping

1. Q1/Q4: trace the sampler and NystromRows algebra from current source; report the exact positivity
   check before denominator/outlier normalization. Distinguish negative g coefficients from a
   negative evaluated affinity sum.
2. Q2/Q3: extend the existing temporary METHOD-079 probe to record every native target sum in every
   attempted prefix configuration, and all invalid row IDs/coordinates. Separate visited flags prove full capture; genuine nonfinite sums remain findings. A failing chunk continues
   recording its later rows, while the attempt still rejects; no accepted algorithm is changed.
   Save normalized target coordinates once and existing snapshot operands for each target-failing
   attempt. Preserve all earlier METHOD-079 evidence unchanged.
3. Q2/Q3/Q4: offline bounded analysis of those saved attempts. CSV per target: ID, coordinates,
   clutter/inlier fixture label, native sum, nearest-source/target-neighbour/landmark distance in
   sigma. All invalid IDs/sums are retained. For each failing attempt, exact sums/inlier mass use all
   invalid rows up to 1024, otherwise a seeded uniform sample of 1024, plus the first invalid
   and most-negative finite rows; add a seeded uniform sample of 256 valid rows. Landmark-dot
   contributions and rebuilt-row/saved-g sign checks use this same declared sample. Report every invalid ID, counts
   by truth label, quantiles for negative versus positive rows, and illustrative first/most-negative
   rows selected before exposure. Do not call all clutter rows ignorable or infer all variants fail.
4. Q2/Q3: use the four-cell reversed-repeat plan points_1 (7 iterations), box_1 (2), box_2 (2), points_2 (7).
   This includes all target-failing draws observed in METHOD-078: points 3/4/6, box 1.
   A small standalone report uses the existing trace parser and fixture preview for fresh
   losses/stages/before-after plots, plus census HTML/CSV and spatial plots. Original artifacts
   stay immutable. Exact repeats test deterministic identity, not statistical uncertainty.
5. Q1–Q4: independent raw audit and scoped ARA record; preserve patch, remove all native probes,
   run original focused Accelerated tests and structural checks. Stop after answering this layer.

## Reuse and scope

Reuse METHOD-079 candidate/probes.patch and snapshot format, fixture preview and METHOD-078
trace parser. Do not rerun the old first-row analyzer: exact snapshot byte equality binds its
existing outputs. Write explicit new commands/report to avoid mutating old module globals. New behavior:
collect all target rows and per-failing-attempt snapshots, plus one offline census script.
Use installed NumPy/SciPy cKDTree/Matplotlib for analysis; no added dependency, public API, UI,
config, export or test-registry change. Native callers: existing NystromRows/Evaluate and benchmark
runner only. One writer, existing ci-release build. No source/target subsampling in the fixture.

## Verification and run scope

Before numbers are read: analytic exact-sum, nearest-neighbour, signed-dot and full-row collection
controls (including a negative row after the first invalid row in the same chunk); assert the real
prefix endpoints match the METHOD-079 baseline executable at the same iteration limit, and first
snapshots match immutable METHOD-079 exactly; all expected attempt
sum buffers have N visited flags set exactly once. Report negative, zero and nonfinite sums
separately; preserve nonfinite findings. Native first-invalid sums must match their snapshots
bitwise; the independent exact-sum oracle must match old first-row values within 1e-12 relative. Mutation-check the new collection control by restoring the
old early return. Native max 90s; offline census max 180s per cell, child memory 8 GiB, total 1200s,
all enforced. A resource/technical failure is unevaluated, not an adaptive permission to reduce rows.

This is an exploratory descriptive census on synthetic data with known clutter labels. No adoption,
parameter or performance decision rests on it, and it is not confirmation: light preflight tier
(one fresh independent reviewer, exact bounded packet, known-answer controls and final-path technical
check). Any decision about a fix or broader sampling study is explicitly outside this packet.

Done: all four questions answered with exact sampling semantics, complete invalid-row lists for
these attempted prefixes, measured geometric characteristics and signed stage decomposition;
reviewed report retained, production probes removed. Commands: ci-release native runner build;
script controls/technical then reviewed census run; ci IntrinsicGeometryRegistrationSlowTests and
ctest -R '^CoherentPointDriftAccelerated\\.'.

## Counter-review disposition

Claude plan review accepted with corrections: points prefix extends to 7 iterations; complete
collection uses visited flags; exact oracle gets frozen real-row cross-checks; invalid/valid
comparison groups and per-label rates are explicit; exact work is bounded by the seeded sample
cap above. NumPy PCG64 seed 8001+draw*10000+L (invalid sample), plus 1 (valid sample), no stride.
Every census/plot distinguishes full counts from sampled exact masses, records coordinates in
normalized space, and excludes self from target-neighbour distance. Report cancellation ratio
and whether a failed target coincides with a landmark; negative g alone is not an error.
Technical checks time the maximum sampled row count without reading outcomes. Only the native
and census steps run, with no separate old analyzer. Sampling is exploratory description only,
not a causal geometry claim or adoption decision. The report states untested methods explicitly.
