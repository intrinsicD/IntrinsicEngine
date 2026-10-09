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
farthest point, seed 0, and the cell's source / schedule / pattern. Negative control:
`points/mixed/random` with `NystromLandmarks 2`, `NystromErrorLimit 1.0`.

## Run plan (72 cells, enforced by the driver)
For fixture in (full, partial), seed in (1, 2, 3), in this order:
1. `exact` (writes the reference positions),
2. the 9 variants `points/{mixed,fixed,resampled}/random`,
   `box/{mixed,fixed,resampled}/{random,halton}`,
3. `points/mixed/random#repeat` (same command again),
4. `negative_control`.

Timeout 600 s per cell (technical test: about 90-110 s per cell at 10^5); a timeout or a missing
reference records the cell as unevaluated. Results are appended to `results.jsonl`; recorded
cells are skipped on restart; nothing is overwritten. Estimated total about 2 hours.

## Commands
```bash
python3 tools/benchmark/cpd_landmark_variants.py \
  --runner build/ci-release/bin/IntrinsicCoherentPointDriftLandmarkVariants \
  --out ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/runs \
  --positions build/method077_positions
```
Plan listing: the same command with `--plan`.

## Outputs per cell (JSON)
`status`, `termination`, `iterations`, `nystrom_iterations` (iterations whose E-step was
Nystroem), `max_sampled_error`, `kernel_evaluations` (`CPD::Result::KernelEvaluations`),
`seconds` (wall clock of `CPD::Register`, fixture construction excluded), `sigma2`, `scale`,
`backend`, `rms_to_truth` (RMS over all source points of |T(y_i) - truth_i|, world units),
`rms_to_exact` and `max_to_exact` (RMS / max over all source points of the distance to the same
fixture and seed's `exact` result, world units).

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
