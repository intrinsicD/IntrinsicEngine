# METHOD-077 pre-flight — Layer A (what runs)

## Code
- Library baseline: commit `90214f4a5` (METHOD-076: selectable Nystroem landmarks); no library
  file differs from it in the run commit.
- Run commit: the commit that adds this revision of the packet; it contains the runner
  `benchmarks/runners/CoherentPointDriftLandmarkVariantsRunner.cpp` (target
  `IntrinsicCoherentPointDriftLandmarkVariants`, `benchmarks/CMakeLists.txt`), the driver and
  frozen analysis `tools/benchmark/cpd_landmark_variants.py`, and its test
  `tests/regression/tooling/Test.CpdLandmarkVariantsAnalysis.py`.
- Code under measurement: `src/geometry/Geometry.Registration.CoherentPointDrift.{cppm,cpp}`,
  `src/geometry/Geometry.Registration.CoherentPointDrift.EStep.{cppm,cpp}` (`BuildLandmarks`,
  `UnionBoxPoints`, `NystromRows`, the Nystroem driver in `Evaluator::Evaluate`),
  `src/geometry/Geometry.PointSampling*.cpp` (landmark orders).

## Environment
AMD Ryzen 9 7950X (16 cores, 32 threads; logical CPUs 0-15 are one per core), 62 GiB RAM,
Ubuntu clang 23.0.0, preset `ci-release` (Release). One cell at a time, pinned with
`taskset -c 0-15` (the driver refuses to run without `taskset`), 16 solver threads. The run uses
this checkout at the frozen commit; nothing is committed while it runs. Peak memory at 10^5 points about 394 MB (technical test).

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
- Target `partial`: the same draws, keeping only clutter points and points whose truth has
  x < 0.2 (47355 targets at seed 1); a subset of `full`.
- Raw data looked at (`--dump 3`, seed 1, both fixtures): counts and the first and last three
  source, truth and target points.

## Configuration (every cell)
Bayesian CPD: `OutlierWeight 0.1`, `Beta 1.0`, `Lambda 2.0` (normalized units, `NormalizeInputs`
true), `Gamma 1`, `Kappa inf`, posterior-variance terms off, `LowRank 100` (200 farthest-point
landmarks for G, identical in every cell), `MaxIterations 150`, tolerance 1e-5, `Threads 16`,
landmark seed = fixture seed (`LandmarkSampling.Seed`; farthest point ignores it, so G and point
landmarks do not change with it; box streams, Halton shifts and resampled seeds do).
- `exact`: E-step `Auto`, which picks truncated (row error <= 1e-6), fast Gauss (<= 1e-6) or dense
  per iteration; the mix is not recorded.
- Variants: E-step `Nystrom`, `NystromLandmarks 256`, `NystromErrorLimit 1e-3`, farthest point,
  and the cell's source / schedule / pattern (for point sources the pattern field is unused;
  `points/resampled` shifts the farthest-point start index with the draw). Each iteration starts
  at the last landmark count that passed (at least 256) and doubles on rejection while all
  attempts stay below half the dense cost (effective cap 1024 on `full`, 512 on `partial`); a
  rejected iteration runs exactly (`Auto` without fast Gauss), and Nystroem is not tried again at
  or below that sigma^2.
- `perturbation_control`: `points/mixed/random` with `NystromErrorLimit 1e-5`.
- `floor_control`: `points/mixed/random` with `NystromErrorLimit 1e-300` (an attempt passes only
  at a sampled error of exactly 0, so practically every iteration takes the exact fallback).
- `negative_control`: `points/mixed/random` with `NystromLandmarks 2`, `NystromErrorLimit 1e300`
  (no error rejection; an attempt is still rejected when the landmark eigendecomposition fails,
  its top eigenvalue is not positive, or a sum is not positive or not finite, and a rejection
  doubles the landmarks).

## Run plan (138 cells, enforced by the driver)
For fixture in (full, partial), seed in (1, 2, 3), in this order:
1. `exact` (writes the reference),
2. pass 1: the 9 variants `points/{mixed,fixed,resampled}/random`,
   `box/{mixed,fixed,resampled}/{random,halton}` and `perturbation_control`, each `@1`,
3. `floor_control`, `negative_control`,
4. pass 2: the same 10 timed arms in reverse order, each `@2`.

Timeout 600 s per cell (technical test: about 90-110 s per cell at 10^5; estimated total about
4-4.5 hours). The exact cell moves any old reference to `runs/stale/` first; only a successful, complete exact
run writes `runs/references/<fixture>_<seed>.bin` (exactly 10^5 double3, size checked) and its
sha256 is recorded; the other cells of the pair run only against that recorded reference,
otherwise they are recorded as unevaluated without running. Each cell writes its own JSON
(`runs/cells/`), which must name the cell's fixture, seed, points, threads, variant, landmarks
and error limit. Every record carries the git commit, the runner's sha256 and the point count;
a restart refuses records of another commit, runner or size and skips recorded cells; a lock
file prevents two drivers. A timeout records the cell as unevaluated. Records are appended to
`runs/results.jsonl`; an unjournaled cell JSON or reference found before a cell runs is moved to
`runs/stale/`, never deleted or overwritten. Exit 0 when every cell completed, else 1. The
analysis runs at the frozen commit with `--runner`; it refuses unless every record carries that
commit, that runner's sha256, 10^5 points and pinning, and every planned cell has a record; it
writes `analysis.json`, or `analysis.<k>.json` when an earlier analysis exists.

Final-configuration technical test (not evidence; status and time only): `partial`, seed 2,
`box/resampled/halton` at 10^5 points, pinned: completed, 150 iterations, about 56 s.
Driver technical test (2000 points, not evidence): 138 cells completed; with every exact cell
failing (earlier driver revision), all dependent cells unevaluated without running; a restart
with another point count and an unknown `--only` cell are refused.

## Commands
```bash
python3 tools/benchmark/cpd_landmark_variants.py \
  --runner build/ci-release/bin/IntrinsicCoherentPointDriftLandmarkVariants \
  --out ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/runs
python3 tools/benchmark/cpd_landmark_variants.py \
  --runner build/ci-release/bin/IntrinsicCoherentPointDriftLandmarkVariants \
  --out ara/evidence/diagnostics/method077_cpd_landmark_variants_20261009/runs --analyze
```
Plan listing: the first command with `--plan`. Analysis test:
`python3 tests/regression/tooling/Test.CpdLandmarkVariantsAnalysis.py`.

## Outputs per cell (JSON)
`status`, `termination`, `iterations`, `nystrom_iterations` (completed iterations whose E-step
was Nystroem; the final convergence-check E-step is not counted), `max_sampled_error`,
`kernel_evaluations` (`CPD::Result::KernelEvaluations`: kernel terms of the E-steps; a Nystroem
attempt rejected before its last pass is undercounted, only n L of its terms are added; landmark
selection, the landmark eigendecompositions of about 2 L^3 and the G setup are not counted),
`seconds` (wall clock of `CPD::Register`), `sigma2`, `scale`, `backend`, `kernel_rank` and
`kernel_error` (the low-rank G's rank and sampled relative error; G is identical in every arm),
`rms_to_truth` (RMS over
all source points of |T(y_i) - truth_i|, world units; null for an incomplete result),
`rms_to_exact` and `max_to_exact` (RMS / max over all source points of the distance to the same
pair's `exact` result, world units; null with `reference_error` when no comparison was possible).
Runner exit codes: 0 complete, 1 incomplete result, 2 usage, 3 no comparison possible.

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
