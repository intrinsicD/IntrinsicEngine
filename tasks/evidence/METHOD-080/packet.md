# METHOD-080 — Light preflight: descriptive invalid-row census

1. "Bei welchem sampling Verfahren passiert das?"
2. "Kannst du untersuchen welche Zeilen das genau sind?"
3. "Was zeichnet diese Punkte aus?"
4. "An welcher Stelle werden die affinitätssummen negativ?"

Operator-directed continuation of METHOD-079, outside Framework24 P0. The shortest path is
complete row capture on the same synthetic fixture and already observed failing attempts.
This is exploratory description with known synthetic truth. No sampler adoption, tuning,
production remedy, confirmation, general causal or performance decision rests on these cells.
Light tier therefore applies under the synthetic-known-truth rule. One fresh independent
reviewer must PASS this committed packet and its commit/SHA256 binding before execution.

## Questions, prediction and limits

Q1/Q4 are answered from source and native trace. The points arm is FarthestPoint, first index 0,
fixed point identities, half target/half source, with current source coordinates. The variant's
`random` suffix is a box pattern and is ignored for Points. The other arm resamples uniform
random points in the union of current sigma-padded boxes, using seed 1 plus draw. No other
sampler is tested and this is not a controlled sampler comparison.

For Q2/Q3, the descriptive hypothesis is that invalid rows occupy different geometric/support
regions from valid rows in the same attempt. Compare the frozen groups below without a causal
verdict. Prediction: the already known first-invalid rows and native arithmetic replay exactly;
all further row counts and characteristics are unknown. If geometry/mass groups overlap, report
that without selecting new points, thresholds, cells or hypotheses. Ineffective instrumentation
would miss later rows, discard nonfinite values, change the accepted path or mis-scale weights;
the controls below reject these failures. No inferential significance or population uncertainty
is claimed. Exact repeated seeds test deterministic replay, not generalization. Complete native
counts resolve one row; sampled exact masses describe only their declared samples.

## Frozen cells, inputs and commands

Order: `points_1 points/fixed/random 7`, `box_1 box/resampled/random 2`,
`box_2 box/resampled/random 2`, `points_2 points/fixed/random 7`.
All use fixture `full`, seed 1, N=M=100000, eight native threads pinned to CPUs 0–7,
initial L=256, relative E-step error limit .001; no source/target subsampling.
Other BCPD/fixture defaults are exactly those in the committed runner. The seven-iteration
prefix includes the three known points target failures at draws 3/4/6, L=256/512/1024;
the two-iteration box prefix includes draw 1 failures at L=512/1024. Draws are zero-based.
The fixture replaces every twentieth target with uniform clutter (5000 labels, 95000 regular
targets). This label is distinct from posterior inlier probability. Current moved source,
normalized target and sigma come from each attempt, never from the initial fixture.

From repository root, the only results-bearing command is:
```sh
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 python3 tasks/evidence/METHOD-080/census.py run
```
`native_command()` resolves each native invocation to:
```text
taskset -c 0-7 /tmp/cpd-nystrom-census-20261011/bin/native --fixture full --seed 1 --points 100000 --threads 8 --iterations ITERATIONS --trace 1 --variant VARIANT --landmarks 256 --error-limit 0.001 --out tasks/evidence/METHOD-080/runs/NAME.json --positions tasks/evidence/METHOD-080/runs/NAME.bin
```
Paths are resolved absolutely by the driver. `INTRINSIC_NYSTROM_CAPTURE` is the absolute
`runs/NAME.snapshot` prefix. Analysis is `taskset -c 0-7 <current Python> census.py analyze
<absolute runs directory> NAME`. The standalone report reuses METHOD-078's trace parser and
fixture preview; the old first-row analyzer is not rerun. Native build command:
`cmake --build --preset ci-release --target IntrinsicCoherentPointDriftLandmarkVariants`.
`binary-bindings.json` binds executable, source, Python and installed numerical library versions.

## Measurement and gates

Capture every reached target-pass attempt: uint64[4] `(80,N,draw,L)`, double[3]
`(sigma2,logC,LogWeightShift)`, N double native shifted sums, N uint8 visit counts.
Every count must equal one; negative, zero, nonfinite and positive are separate findings.
Normalized targets: three N-double arrays X/Y/Z. Existing format-79 operands are retained for
every invalid attempt. All raw data survive. `NYSTROM_SPECTRUM`, native attempt decisions and
captured attempts must match; an unexpected failure before the target pass is an explicit
technical stop, not silently classified as a complete census. An invalid attempt must still
return false before h/q/source posterior accumulation. First-invalid native sum and snapshot
must match bitwise. Same first snapshots must equal METHOD-079 byte-for-byte, binding its
previous extended-arithmetic analysis, which is not generalized to new rows.

Every invalid ID, normalized XYZ, fixture label and native shifted sum is listed. For all rows
in each failing attempt compute nearest moved-source, nearest other target (self excluded,
duplicate coordinates allowed) and nearest landmark distance, divided by that attempt's sigma.
Report min/median/p95/max within negative/positive × clutter/regular groups, negative rates
with denominators 5000/95000, and invalid rows coinciding exactly with landmarks.

For each failing attempt: all invalid rows if at most 1024; otherwise uniform PCG64 sample 1024,
seed `8001+draw*10000+L`. Add first invalid and most-negative finite row regardless of inclusion.
Add up to 256 uniform valid rows using seed+1; no stride aligned with fixture labels. Exact
mass quantiles use these declared samples, stratified by label/validity. This is descriptive,
not an unbiased population estimate (the two explicitly selected examples can be included).
Exact calculations are one row at a time in NumPy long double:
`log S = logsumexp(lw_i - ||x-y_i||²/(2 sigma²))`,
`log A = log S + LogWeightShift`, `inlier_mass = sigmoid(log A-logC)`.
Log values are preserved when exponentiation underflows. Compare native shifted sum to shifted
exact sum. The first-row log difference must imply relative error <1e-12 against METHOD-079's
independent exact oracle. Per-landmark positive/negative dot terms, cancellation ratio
`sum(abs(terms))/abs(sum(terms))` and rebuilt-Gaussian/saved-g sign are reported. These are
landmark-basis terms, not source contributions; saved-g replay cannot establish a newly
recomputed eigensystem's sign or exact-arithmetic truth.

Each cell's HTML includes all attempts and rejection causes, full-ID CSV links, sample counts,
geometry plots, every recorded objective/NLL/sigma/policy value over iterations, stage events,
and before/after fixture views. Census geometry uses normalized coordinates; before/after
views use world coordinates. The implemented stopping objective has NLL weight/share 1 and
extra prior weight/share 0; no full ELBO is claimed. E-step sigma and post-M-step sigma are
distinct. All comparisons are within an attempt. Repeats report raw-buffer byte equality and
numerical-summary equality without interpreting timing as a result.

## Controls completed before exposure

`verification/test-controls.py`: weighted Gaussian sum `1+2 exp(-.5)`, log underflow,
outlier/shift sigmoid, analytic signed 2-landmark Gaussian counterexample, duplicate-aware
self exclusion, uniform seeded selection, preserved negative/zero/nonfinite row categories,
missing-visit rejection and optimized-Python refusal. Removing weights, shift or visit gate
makes its known-answer control fail. Native early-return mutation separately makes complete
capture fail while preserving rejection/registration endpoint.

`verification/test-technical.py`: both final real configurations, no numerical outcomes read.
Endpoint bytes and trace fields excluding time match the previous executable at the same
iteration limits; all attempt coverage/native rejection/first-row replay gates pass, full
analysis and report path run, failed/missing analysis cannot get a result. The initial trace
comparison incorrectly included wall time and was corrected to compare the eight scientific
fields only; the reuse initially assumed two parser results and was corrected to its actual
five-value API. No scientific outcome was exposed by these preparation repairs.
`verification/test-cap.py`: maximum 3×1282 exact rows on a 100000-source analytic fixture pass
the 180s/8 GiB resource limits; every exact value has a known assertion. Logs retain measured
peak RSS for native/analysis children at both final configurations and the cap control.
Null/collapse controls for registration adoption are not applicable: no method efficacy,
convergence, quality or ranking verdict is made. Missing capture, erased signs, wrong scaling
and accepted invalid attempts are the relevant false successes and are directly controlled.

## Start/stop and reporting rules

Commit packet/status/code first. Review record outside packet contains commit, packet SHA256,
actual reviewer/session/verdict and hashes of all loaded code/data/binary inputs. Before every
native/analysis start the driver verifies reviewed commit, clean tracked tree and all input
hashes (including this packet); concrete cells come only from the committed PLAN.
The initial gate also checks the four recorded Python/library versions against the environment.
Native90s, analysis180s per cell, child address space8GiB, total1200s including report generation, enforced
in code. No adaptation/rerun after a scientific surprise; continue the four authorized cells.
Timeout, crash, IO/shape/replay/coverage failure stops with UNEVALUATED report and retained raw
outputs. Changing the plan needs a new review. Technical preparation outputs are not evidence.
After execution: independent raw audit, ARA claim before narrative, preserve temporary patch,
restore production source, original focused Accelerated tests and structural checks. The
independent reviewer decides the three task acceptance items. No production remedy is in scope.
