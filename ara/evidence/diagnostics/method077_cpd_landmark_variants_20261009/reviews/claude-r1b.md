I reconstructed what the run measures from Layer A and the code only; no verdict yet.

**Rule compliance:** I read `layer-a.md`, the runner, `CoherentPointDriftScalingFixture.hpp`, the driver, the relevant parts of the CPD and E-step `.cppm/.cpp`, and the `Params` defaults in `PointSampling.cppm`. I did not open `layer-b.md`, the task files, `research_notes/`, `reports/` or any result file. I listed the file names in the experiment directory: `runs/` does not exist yet. I broke no rule.

### 1. What a cell measures
Each cell is one Bayesian CPD registration in a fresh process: 10^5 source points onto a target of 10^5 points (`full`) or about 47k points (`partial`).

- **`exact`:** E-step `Auto` (dense, or truncated / fast Gauss with a 1e-6 bound). Only a complete run writes its `TransformedSource` (world units, double3) as the reference.
  - The driver records the file's sha256 and checks it again before every dependent cell.
  - The runner also refuses a file of the wrong size.
- **Variants:** a Nystroem E-step with the cell's landmarks.
  - Each iteration starts at the last landmark count that passed and doubles on rejection, up to the cost cap. By my arithmetic the cap is 1024 landmarks for `full` and 512 for `partial`.
  - If every attempt is rejected, the iteration runs `Auto` without fast Gauss.
  - After that, `NystromRejectedSigma2` keeps every later iteration exact unless sigma² rises again.
  - So each variant run is a Nystroem stretch at the start followed by an exact stretch.
- **Metrics:**
  - `rms_to_truth`: RMS of |T(y_i) − truth_i| over all source points, world units. In `partial`, about half of these points are extrapolated.
  - `rms_to_exact` / `max_to_exact`: per-point distance to the reference of the same fixture and seed, as RMS and max, world units. They measure the deviation of the whole run, end to end.
  - `seconds`: wall clock of `Register`, including the G setup, landmark selection and eigendecompositions.
  - `kernel_evaluations`: a count of kernel terms. It includes the final E-step, which only detects convergence.
  - `nystrom_iterations`: completed iterations whose Nystroem E-step was accepted. That final E-step is not counted.
  - `max_sampled_error`: dimensionless, accepted iterations only. It is the maximum over:
    - 32 fixed target rows, whose denominators are compared with exact ones (in `full`, 8 of these rows are clutter points);
    - 32 fixed source rows, whose P1 is compared with exact kernels divided by the *approximate* denominators.
  - Also `sigma2` (world units²), `scale`, `termination` and `backend`.
  - `landmarks` is the *requested* count, not the count actually used.
- No statistic across cells is computed in code.

### 2. What an ineffective landmark method would produce
- **The variant setting does nothing (a wiring fault):**
  - Every count would be bitwise identical to that pair's `points/mixed/random`, with `seconds` within the repeat's spread.
  - The JSON only echoes the command-line string, so that identity is the only evidence.
  - For `points/fixed` the identity is ambiguous. Farthest-point order does not change under a similarity transform, only under the deformation, so Mixed can legitimately pick the same landmarks as Fixed.
- **Never accepted:**
  - `nystrom_iterations` is 0, `backend` is `cpu_auto`, `max_sampled_error` is 0.
  - `rms_to_exact` is about the floor control's value. That should be 0 if `exact` never chose fast Gauss and is deterministic.
  - `kernel_evaluations` and `seconds` are at or above those of `exact`.
  - This is exactly the floor control's signature.
- **Accepted but wrong:** larger `rms_to_exact` and `max_to_exact`, possibly with a different `termination` or iteration count.

### 3. False successes that would score well
- **A variant that is always rejected** has the best accuracy and no benefit, yet the driver records it as `completed`. That label only means exit code 0 and a matching JSON. The data expose it, but neither the driver nor Layer A enforces a check; that is up to Layer B's gates, which I have not read.
- **Early convergence:** a premature `Converged` on the approximate objective gives few iterations and low cost. It shows only in the `rms_*` metrics and `termination`.
- **The negative control may not be negative:**
  - Non-positive sums double the landmark count.
  - If the doubling reaches the cost cap, the sigma² latch makes the rest of the run exact, and that exact stretch may undo the error.
  - Its actual landmark count is not recorded.
  - `kernel_evaluations` undercounts these rejections. An early return leaves `evaluations` at 0, so only n·L is added, although L² + m·L (pass 2) or L² + 2m·L (pass 3) more were evaluated. Layer A says every attempt is counted, which does not hold here.
- **An `rms_to_truth` below `exact`'s** is not a success; it can come from chance or extra smoothing.

### 4. What the run cannot distinguish
- **Approximation quality versus trajectory and stopping effects.** Every variant ends with exact iterations, so `rms_to_exact` may just be convergence noise at tolerance 1e-5. No control gives the size of a harmless perturbation: the repeat is bitwise identical and the floor control runs the exact path.
- **Landmark placement versus landmark count.** The count depends on the run's path and is not recorded.
- **Random versus Halton beyond a single draw.** The landmark seed is 0 in every cell, so the random stream and the Halton shift are the same for all fixture seeds; only the box size changes. In effect this is one draw.
- **What a non-zero floor means.** Fast Gauss inside `exact` and non-determinism in `exact` look the same. `backend` `cpu_auto` hides which policies ran, and `#repeat` only covers the baseline variant.
- **Timing.** There is one sample per cell, in a fixed order (box variants always run later), with only 6 repeat pairs.
- **Fixture versus landmark budget.** The caps differ (1024 vs 512).
- **Anything beyond one surface, deformation, clutter rate, parameter set and size.**

### 5. Execution risks
- **Timeouts:** the limit is 600 s against about 100 s expected. A timeout of `exact` deletes the reference, so that pair's 12 dependent cells become unevaluated, as Layer A states. `failed` and `unevaluated` are permanent, so a transient fault loses the cell.
- **Resume:** existing records' `points`, `commit` and `runner_sha256` are not compared with the current run, and `matches()` ignores `points`. A rebuilt runner or a `--points` override would mix records silently. There is also no lock against two drivers, no check for a dirty working tree, and a failed `git` call is recorded as `""`.
- **Reference size:** the driver does not check it. A truncated reference would be recorded as `completed`, and each dependent cell would then fail loudly (exit 3) after about 100 s.
- **Broken last line:** a partially written last line in `results.jsonl` makes the restart crash in `json.loads`. That is loud, but needs a manual edit.
- **Non-finite values:** a `nan` or `inf` printed by C++ is invalid JSON and would crash the driver. The risk is low, because the displacement and sigma² are checked for finiteness.

Not checked: the claim that no library code differs from `90214f4a5` (I have no git access), the M-step's determinism, and whether `PlanFastGauss` has side effects.