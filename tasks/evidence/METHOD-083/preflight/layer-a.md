# Execution specification

Local CPU diagnostic ID `method083_upstream_nystrom_alignment`; diagnostic JSON, no
schema-v2 performance claim. Repository `/home/alex/Documents/IntrinsicEngine`.
Full-tier review by two fresh Codex sessions. Implementation and native adapter have
separate independent approval; this packet is a new execution approval.

## Code and binaries
Baseline engine `ff5496965`, adapted engine represented by `../implementation.patch`
SHA256 `453688ab401e2d47bf0f16ac178baa9237ede1f6e52d728bba2ee03330ac479b`.
`../build-runners.py` built one source adapter (`../diagnostic-runner.cpp`, SHA256
`45dac2a54bd5bb6d84ed063aeffb0b3d5e1ed9e6b60702628222b017b5c4a7a9`) against both geometries,
using ci-release Clang23/matching scanner and the existing native benchmark target.
The adapted build defines METHOD083 for its new controls/trace flag; baseline emits zero
for that unavailable flag. Source geometry hashes and executable hashes are in
`../verification/{baseline,adapted}-binding.json`.
Baseline binary SHA256 `b9cf377f2a18fe89b0585848082dce255262c43c4636bf84fc8942d04bb7bc1a`;
adapted binary `fd25e025df23ede7d5b76c6e8bccfd81323e53ae05fe26c40e8c1dfadb21a329`.
Executables reside in `/tmp/method083/bin/`.

Upstream is unmodified https://github.com/ohirose/bcpd at
`b7b6d01bb703e154f652b6a798c1eacd39b83ca3`, built with its Linux makefile using `make CC=gcc`.
Executable SHA256 `8baab82e389ac51cde49eb832695b5f0fb25dfd0a8a7d8fb064695cf40e635ed`.
The tarball/source hashes are in `../upstream-binding.json`; local source is at
`/tmp/cpd-upstream-build-20261012/ohirose-bcpd-b7b6d01`. GCC is used only for this author's
standalone C program; engine verification uses the repository Clang preset. Runtime
library files, hashes, versions and ldd output are in `../environment.json`.

## Inputs and units
Six stored METHOD-082 fixtures, full/partial × seeds1–3, 100000 source points each:
`tasks/evidence/METHOD-082/fixtures/{full,partial}_{1,2,3}.npz`. These are the prior rounded
float32 dumps, now used directly as the input data for every arm; no internal regeneration.
`../prepare-inputs.py` subtracts each set's own mean, divides both by the larger RMS radius,
then casts normalized inputs to float32. Truth uses target mean and the same scale.
All normalized floats are written as exact-recovering 17-digit decimals to
`/tmp/method083/inputs/`. Original double parser and engine float parser therefore receive
identical representable coordinates. Inverse-frame assertions are within1e-6 world units.
`../input-binding.json` binds every input hash/count and each transformation.

Internal normalization is disabled in all executables. Endpoints convert back by
`world = solver * shared_scale + target_mean`. Registration quality is evaluated in original
world units against original stored truth and the original unregistered source. This
preserves the old separate-centroid initialization. Float export changes arithmetic
rounding from METHOD-082; this is why all baseline arms are rerun. These observations do
not replace or rewrite the prior run. Fixtures, seed meanings, noise/clutter and partial
visibility are the same cases; coordinates within1e-5 of the partial x=.2 truth boundary
are reported as a separate ambiguity subset. Counts partition the full source set.

## Exhaustive plan and configuration
Exact controller from repository root:
```
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=8 python3 tasks/evidence/METHOD-083/comparison.py run
```
`../comparison.py::cells` and `command` define every cell and native argv. For each full/
partial fixture and seed1/2/3: baseline Auto/FPS/box, adapted Auto/FPS/box/union, upstream
Nyström+KD and upstream direct+KD. FPS is points/fixed/random with default farthest-point
sampling; box is box/resampled/random. Union is union/fixed/random and ignores the separate
G sampling method. Then replay adapted-union and upstream-Nyström for full/partial seed1.
58 processes total. Two slots, fixed logical CPU sets0–7 and8–15, eight engine/OpenMP threads,
OpenBLAS threads1. Fresh processes, no warmup. Timings are descriptive and not ranked.

Engine resolved settings: Bayesian, omega.1, beta1 solver units, lambda2, rank100 G,
Gamma1, infinite Kappa, posterior variance terms off, scale on, reflection off,
no subsampling/device/cancellation, E-step tolerance1e-6, objective relative tolerance1e-5,
initial variance from mean pair distance, sigma² floor1e-10, maximum5000 M-steps.
Landmark seed equals fixture seed; FPS ignores this seed for G. G basis construction stays
identical between old/adapted engine arms. Initial/minimum E-step landmark count256,
error limit.001; accepted count persists and rejections double up to4096 under the existing
half-dense work budget. Actual accepted landmark/repair counts are not exposed in the retained
trace; this is a comparison of complete guarded policies, not matched-rank performance.
Adapted Nyström arms switch at sigma<.2 to the existing bounded Truncated evaluator.
Baseline has no such explicit threshold. Auto is unchanged by that setting.

Upstream resolved argv includes `-w .1 -b 1 -l 2 -g 1 -K 100 -J 256` (or J0), `-n 5000
-N 30 -c .00001 -r seed -u n -T srn -p -f .2 -d 7 -e .15 -h -s ytT`.
Kappa remains its default ZERO sentinel (uniform weights); posterior variance option-a is
absent. Gamma1 avoids the engine's variance-vs-upstream-standard-deviation scaling difference.
Upstream J is fixed256, its G is a distinct random Nyström basis, and its local radius is
min(7 sigma,.15). Its J-dependent union permutation consumes RNG before G construction;
therefore J256 versus J0 also changes G draws. These are native configuration references,
not an isolated E-step ablation. Same numeric seeds do not match different programs' draws.
No coordinate features, covariance input, subsampling or interpolation options are used.

## Stops, observations, validation
Engine convergence is native success+converged; cap and sigma floor are distinct. Upstream
returns its zero-based loop index after sigma<1e-5 or absolute sigma change<1e-5, once
index>30. Index5000 means cap; index<5000 plus successful outputs means native convergence,
with index+1 completed updates. Its history omits the final converging update, while info.txt
contains final sigma. Engine history omits its final convergence-only E-step. These are
different stopping rules; no shared global-optimum/convergence guarantee is asserted.

Engine trace logs iteration, sigma² after update, NLL/objective/mass before update,
actual backend, sampled error, kernel evaluations, elapsed seconds and planned-local flag.
Stage events are retained and rendered against elapsed time. The planned-local flag
is counted as local iterations, not transition events. Bayesian stopping objective equals its logged NLL (weight1,
raw=weighted; share100% of this logged objective). A full ELBO breakdown is not exposed.
Upstream logs sigma, mass and backend at six decimal places, time/sigma profiles, info,
transform and six-decimal endpoint positions. It does not expose NLL/ELBO or complete
trajectory coordinates with this command; those unavailable curves are labeled unavailable,
not zeros or passes. No observer patches are applied. Saving all source trajectories would
reserve12GB per process; this bounded comparison does not request them.

`load`, `metrics`, `verdict` and `report` own validation/analysis. `distances`/`quality`
reuse frozen METHOD-081 arithmetic. The new trace parser is necessary because its old
parser's fixed nine-field schema cannot accept the planned-local flag. Native result/trace
counts, finite values, status/termination, position shapes and native-vs-Python truth RMS
are checked. The unmodified original's info/history indexing is checked separately.
Unknown/malformed outputs are technical invalidity; explicit documented numerical failures
remain scientific failures. Timeout is unevaluated, with its available trace still plotted.

Per-process timeout1800s and address-space cap8GiB; total launch/runtime budget54000s
(29 pairs ×1800 plus1800 overhead). Both are enforced in code. Per-cell peak RSS comes from
/usr/bin/time when available. Technical final-config tests ran the largest/full input for
all nine native configurations and partial union/original configurations through their first
completed-step marker, then killed them deliberately without inspecting result metrics;
all prefixes were below the memory bound. `../verification/terminal.py` additionally
checks normal terminal execution for baseline FPS, adapted union and upstream Nyström
on full/seed1 with the final100k/rank100/5000cap commands. It asserts exit success,
finite endpoints, native counts and output schema via the unchanged reader; no scientific
metrics are printed or interpreted. Raw technical files are retained with SHA256 bindings.
These three terminal paths plus all-nine configuration prefixes cover execution; they are
not quality results. No numerical technical outputs enter conclusions.

Every completed attempt produces refreshed HTML from raw logs, endpoints and metadata:
curves with observed backend transitions, before/after pictures, all-point/subset metrics,
native stop labels, frozen verdicts and replay results. Missing measures remain unavailable, including agreement when the Auto endpoint is
censored. Original final sigma/scale/matched mass must be finite. Replay compares all
observed non-timing trace fields and final status/termination/iterations/sigma²/scale/mass.
If either endpoint is missing, replay is null/unevaluated before any trace comparison.
Scientific failures/counterexamples do not cancel the remaining fixed plan. Changed inputs,
unexpected process failures or invalid measurement output stop the campaign and retain a
partial report; remaining cells are unevaluated. No adaptive tuning, extra seeds or retries.

## Freeze
Commit code, task status and both packet layers before generating the manifest. Exact
manifest argv is `comparison.py::manifest_command()`, which includes the repository inputs,
three binaries, exported datasets, runtime libraries, both layers and selected technical/
control evidence. Mutable preflight, reviews, runs and verification outputs are excluded;
selected evidence is re-added explicitly. Before each process launch, regenerate this
manifest and compare its SHA256 with the reviewed one. Require two actual PASS records
at `../reviews/approval.json`. Reviews are staged: reconstruct this layer before Layer B.
