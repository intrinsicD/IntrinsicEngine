# METHOD-077 pre-flight review, round 1: reconstruction of the run from Layer A and code

**Reading rule:** kept. I read `layer-a.md`, the runner, the fixture header it includes, the driver, the relevant parts of the CPD and E-step library, and the `PointSampling` defaults. I did not open Layer B, tasks, `research_notes/`, `reports/`, any run output or the manifest. Without a shell I could check neither the packet digest nor the claim that no library code differs from `90214f4a5`.

## 1. What each cell measures

- **Fixture (per fixture and seed):**
  - **Source:** 10^5 points on the bumpy ellipsoid.
  - **Truth:** truth_i = 1.1·R(y_i + bend(y_i)) + t.
  - **`full` target:** truth plus N(0, 0.004²) noise; every 20th point is replaced by clutter in [−1.6, 1.6]³.
  - **`partial` target:** the same random draws, minus inliers with truth x ≥ 0.2, so it is a subset of `full`.
  - Source and truth are the same in both fixtures.
- **Solver:** BCPD in normalized units. Kappa is infinite and posterior-variance terms are off (a default Layer A does not list), so all E-step weights are equal. G is rank 100 from 200 farthest-point landmarks, the same in every cell. The run stops when the NLL changes by at most 1e-5 relative; the coherence term is 0 for BCPD (`src/geometry/Geometry.Registration.CoherentPointDrift.cpp:590`, `:832`).
- **`exact` is not dense.** `Auto` picks truncated (row error ≤ 1e-6), fast Gauss (≤ 1e-6) or dense in each iteration. The mix is not recorded; `backend` just says `cpu_auto`.
- **Nyström cells:**
  - **Start:** each iteration starts at L = max(256, the last L that passed).
  - **Acceptance:** an attempt is accepted if 64 fixed-index sampled rows (32 target, 32 source) are positive and within 1e-3 relative error.
  - **Doubling:** on rejection L doubles while total cost stays under half the dense cost. In effect that caps L at 1024 for `full` and 512 for `partial`.
  - **Latch:** if nothing passes, the iteration runs exact (`Auto` without fast Gauss), and Nyström is never retried at a smaller σ² (`src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp:1648-1681`).
- **Metrics:**
  - `rms_to_truth`: RMS of |T(y_i) − truth_i| over all source points, in world units. It includes the clutter-index points and, in `partial`, the more than half of the sources with no target counterpart, which the model only extrapolates.
  - `rms_to_exact` and `max_to_exact`: point-by-point distance to the same fixture and seed's `exact` result, in world units.
  - `nystrom_iterations`: completed iterations with an accepted Nyström E-step. The final converged E-step is not counted.
  - `max_sampled_error`: dimensionless. The gate caps it at 1e-3 (1.0 for the negative control), so it cannot rank variants.
  - `kernel_evaluations`: counts rejected attempts. It leaves out landmark selection, the L³ eigensolves and the G build. A positivity rejection counts only n·L (`EStep.cpp:1556`, `:1668`).
  - `seconds`: includes initialization.
  - `sigma2`: world units². `scale` is dimensionless; the truth is 1.1.

## 2. What a no-op or ineffective method would produce

- **Never accepted:** `nystrom_iterations` 0, `max_sampled_error` 0, `backend` not `cpu_nystrom`, cost at or above `exact`. `rms_to_exact` and `max_to_exact` are exactly 0. The one exception: if `exact` used fast Gauss somewhere, which the Nyström fallback cannot (`EStep.cpp:1680`), they show a small floor.
- **Accepted only in the first iterations:** almost the same picture.
- **Wired but inert:** every metric bitwise equal to `points/mixed/random` except `seconds`.

## 3. Degenerate solutions and false successes

- **The no-op wins any accuracy-only comparison.** Accuracy is confounded with how many iterations were approximated. Only a gate tied to `nystrom_iterations`, kernel evaluations or seconds catches this, and Layer A defines no gates.
- **A failed `exact` reports `rms_to_truth = 0`.** The sum runs over an empty result and is still divided by N (`benchmarks/runners/CoherentPointDriftLandmarkVariantsRunner.cpp:164-179`). This lands inside a "failed" record; only a status filter catches it.
- **A failed variant loses its status and all metrics.** It exits with code 3, "cannot compare with the reference" (`:183-186`), which looks the same as a reference fault.
- **The negative control is not "2 landmarks, everything accepted".**
  - Positivity failures and errors above 1.0 still reject, and L then doubles toward 1024 or 512.
  - The L actually used is not recorded.
  - If it never passes, it becomes the no-op and scores ≈ 0.
  - It means something only if `nystrom_iterations` > 0, and it shows a gross effect, not a detection limit.
- **`rms_to_exact` assumes `exact` registers well.** Its `rms_to_truth` is the only check, and Layer A sets no bound for it.

## 4. What this run cannot distinguish

- **Landmark quality vs acceptance dynamics** (latch, carried-over L, doubling). L and acceptance are not recorded per iteration.
- **Where differences from `exact` come from.** Several sources mix in `rms_to_exact`, with no perturbation floor:
  - the approximation itself;
  - a different stopping iteration in the slow EM tail;
  - general sensitivity of the EM path;
  - the fast-Gauss mismatch.

  `#repeat` is the same deterministic command, so it only checks determinism and timing.
- **Box variants are partly judged by the fixture.** The clutter cube sets the target box, so landmark density near the surface is low by design. Farthest-point selection likewise spends target landmarks on clutter.
- **Two labels do less than their names say.** The `random` pattern does nothing for point sources. `points/resampled` is farthest-point sampling with a start index shifted by the draw number (`EStep.cpp:1378`), a weak re-draw.
- **The acceptance check sees fewer than 64 useful rows.** In `full`, 8 of the 32 sampled target rows are clutter (j = 3125r, so every 4th is a multiple of 20). Row 0 of each set is a landmark for `points/{mixed,fixed}`, so it is nearly exact, and the check is slightly weaker for those variants.
- **Narrow scope:** 3 seeds per fixture; one N, one L, one error limit, one deformation; the landmark cap differs by fixture.
- **Timing:**
  - one sample per cell, in a fixed order within each block;
  - threads are not pinned on a 16-core, 32-thread CPU;
  - drift within a block is confounded with position, and the repeat gives only one early/late pair per block.

## 5. Execution risks

- **A failed `exact` contaminates its whole block.** It writes an empty reference file, so the driver's existence check passes. All 11 dependent cells then run in full (about 20 min) and are recorded as "failed" (code 3), not "unevaluated" as Layer A says.
- **References are not tied to the run.**
  - Files in `build/method077_positions` have no digest and are not deleted before `exact`.
  - `ReadPositions` accepts any file of at least N·24 bytes.
  - If `exact` times out or crashes while an old file exists (for example from the technical test), variants are silently compared against the wrong reference.
  - The references sit outside the evidence directory, so a later audit cannot recompute the distances.
- **`last.json` is shared by all cells.** If the runner exits 0 or 1 without managing to write it, the driver attributes the previous cell's JSON. It never checks fixture, seed, variant or landmarks against the cell.
- **No commit or binary digest is stored per record.** A resume after a rebuild would mix binaries silently.
- **Any earlier record skips that cell for good.** For example, a `--only` smoke test of a variant before its `exact` records "unevaluated" permanently.
- **Timeouts drop the slowest runs.** They are recorded as "unevaluated" instead of as slow. The margin is about 5× (600 s against 90–110 s); the worst case is 12 h.
- **Determinism:** fixed-chunk reductions should make `#repeat` bitwise equal apart from `seconds`. The fixture's random-number output depends on the C++ standard library, which is fine within one build.

I did not read tests or git history and ran nothing, so every finding comes from reading the code. The biggest risk is the empty or stale reference file, which can silently ruin a whole fixture-and-seed block.