# METHOD-077 pre-flight — Layer A (what runs)

## Code
- Baseline for the variants: commit `90214f4a5` (METHOD-076: selectable Nystroem landmarks).
- Run commit: the commit that adds this packet, the runner
  `benchmarks/runners/CoherentPointDriftLandmarkVariantsRunner.cpp` (target
  `IntrinsicCoherentPointDriftLandmarkVariants`, `benchmarks/CMakeLists.txt`) and the driver
  `tools/benchmark/cpd_landmark_variants.py`. No library code differs from `90214f4a5`.
- Code under measurement: `src/geometry/Geometry.Registration.CoherentPointDrift.{cppm,cpp}`,
  `src/geometry/Geometry.Registration.CoherentPointDrift.EStep.{cppm,cpp}` (`BuildLandmarks`,
  `UnionBoxPoints`, `NystromRows`, the Nystroem driver in `Evaluator::Evaluate`),
  `src/geometry/Geometry.PointSampling*.cpp` (landmark orders).
- Metric code: the runner (`rms_to_truth`, `rms_to_exact`, `max_to_exact`, trace counters).

## Environment
AMD Ryzen 9 7950X (16 cores, 32 threads), 62 GiB RAM, Ubuntu clang 23.0.0, preset `ci-release`
(Release). One run at a time, 16 solver threads. Peak memory at 10^5 points: about 394 MB
(technical test, `/usr/bin/time`).

## Build
```bash
cmake --preset ci-release
cmake --build --preset ci-release --target IntrinsicCoherentPointDriftLandmarkVariants
```

## Data
Built in, no files: `--points 100000`, fixture seeds 1, 2, 3.
- Source: 10^5 points uniform in the parameters of a bumpy closed surface
  (`CpdScaling::Surface`, seed = fixture seed).
- Truth: y + (0, 0.12 sin 1.8x, 0.1 cos 1.4y), then scale 1.1, rotation 0.3 rad about
  normalize(1, 2, 0.5), translation (0.3, -0.2, 0.1).
- Target `full`: truth + N(0, 0.004^2) per coordinate; every 20th point replaced by uniform
  clutter in [-1.6, 1.6]^3 (5000 points); 10^5 targets.
- Target `partial`: as `full`, keeping only clutter points and points whose truth has x < 0.2
  (47355 targets at seed 1).
- Raw data looked at (`--dump 3`, seed 1, both fixtures): counts and the first and last three
  source, truth and target points.

## Configuration (every cell)
Bayesian CPD: `OutlierWeight 0.1`, `Beta 1.0`, `Lambda 2.0` (normalized units, `NormalizeInputs`
default true), `Gamma 1`, `Kappa inf`, `LowRank 100` (landmarks for G: 200, farthest point,
fixed), `MaxIterations 150`, default tolerance, `Threads 16`. E-step `Auto` for `exact`;
`Nystrom` otherwise with `NystromLandmarks 256`, `NystromErrorLimit 1e-3`, landmark method
farthest point, seed 0, and the cell's source / schedule / pattern (for point sources the
`random` pattern field is unused). Floor control: `points/mixed/random` with
`NystromErrorLimit 1e-300` (never accepted; every iteration takes the exact fallback, which is
`Auto` without fast Gauss). Negative control: `points/mixed/random` with `NystromLandmarks 2`,
`NystromErrorLimit 1e300` (only non-positive sums reject; a rejection doubles the landmarks).

## Run plan (78 cells, enforced by the driver)
For fixture in (full, partial), seed in (1, 2, 3), in this order:
1. `exact` (writes the reference positions),
2. the 9 variants `points/{mixed,fixed,resampled}/random`,
   `box/{mixed,fixed,resampled}/{random,halton}`,
3. `points/mixed/random#repeat` (same command again),
4. `floor_control`,
5. `negative_control`.

Timeout 600 s per cell (technical test: about 90-110 s per cell at 10^5). The exact cell deletes
any old reference first; only a successful, complete exact run writes
`runs/references/<fixture>_<seed>.bin` (exactly 10^5 double3), whose sha256 is recorded; the
other cells of the pair run only when that recorded reference exists with that digest, otherwise
they are recorded as unevaluated without running. Each cell writes its own JSON
(`runs/cells/`), which must name the cell's fixture, seed, variant, landmarks and error limit.
Every record carries the git commit and the runner's sha256. Records are appended to
`runs/results.jsonl`; recorded cells are skipped on restart; nothing is overwritten. A timeout
records the cell as unevaluated. Estimated total about 2.2 hours. Driver technical test (2000
points, not evidence): 78 cells completed; with every exact cell failing, 6 failed and 72
unevaluated without running.

## Commands
```bash
python3 tools/benchmark/cpd_landmark_variants.py \
  --runner build/ci-release/bin/IntrinsicCoherentPointDriftLandmarkVariants \
  --out ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/runs
```
Plan listing: the same command with `--plan`.

## Outputs per cell (JSON)
`status`, `termination`, `iterations`, `nystrom_iterations` (iterations whose E-step was
Nystroem), `max_sampled_error`, `kernel_evaluations` (`CPD::Result::KernelEvaluations`),
`seconds` (wall clock of `CPD::Register`, fixture construction excluded), `sigma2`, `scale`,
`backend`, `rms_to_truth` (RMS over all source points of |T(y_i) - truth_i|, world units; null
for an incomplete result), `rms_to_exact` and `max_to_exact` (RMS / max over all source points of
the distance to the same fixture and seed's `exact` result, world units; null when no comparison
was possible, with `reference_error`). `kernel_evaluations` counts kernel terms of every E-step
attempt including rejected Nystroem attempts; it excludes landmark selection, the landmark
eigendecompositions and the G setup. Runner exit codes: 0 complete, 1 incomplete result,
2 usage, 3 no comparison possible.

## Manifest
```bash
python3 /home/alex/.claude/skills/experiment-preflight/manifest.py \
  --exclude ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/runs \
  --exclude ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/reviews \
  --exclude ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/manifest.txt \
  --exclude research_notes --exclude reports \
  ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-a.md \
  ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/layer-b.md \
  > ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/preflight/manifest.txt
```
