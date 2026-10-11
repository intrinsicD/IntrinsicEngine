# Execution specification

Repository `/home/alex/Documents/IntrinsicEngine`. Local CPU diagnostic identifier
`method082_nystrom_convergence`. Diagnostic JSON format; no schema-v2 performance claim.
Two fresh Codex reviewers replace Claude+Codex per operator instruction "Ok ohne Claude".

Engine/binary bindings are unchanged from `tasks/evidence/METHOD-081/binary-bindings.json`:
baseline engine `a93dcb8c7`, candidate engine `ba11fce6b` (same engine at current HEAD).
Both contain `tasks/evidence/METHOD-081/diagnostic-runner.cpp`; production runner is untouched.
Implementation diff: `tasks/evidence/METHOD-081/reviews/code.patch`.
Baseline binary SHA256 `f380566bf7c6418e92641d3f380f97e20e951b470695e5914aad83f13e507acf`;
candidate `3fce512c7e2965c5ceca8eff264053b5821190e6d64fde80390d5151a75c1e99`.
Both were built with ci-release, Clang23 and matching scanner, target
`IntrinsicCoherentPointDriftLandmarkVariants`; previous independent binding audit is retained.

Exact controller command from repository root:
```
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 python3 tasks/evidence/METHOD-082/convergence.py run
```
`convergence.py::cells()` is the exhaustive ordered plan; `command()` supplies native argv.
For fixture full then partial, seed 1 then 2 then 3, run baseline Auto, baseline points,
baseline box, candidate points, candidate box. Then reverse-order repeats of the four
Nyström arms for full/seed1. Total 34 cells, one process at a time. Every cell uses 100000
source points, 8 threads pinned to logical CPUs0–7, 5000 maximum iterations, 256 landmarks,
sampled relative error limit .001. `points` is `points/fixed/random` (FPS; pattern unused);
`box` is `box/resampled/random`; Auto is runner variant `exact`.

Resolved remaining config: Bayesian, input normalization on, outlier weight .1, beta1,
lambda2, low rank100, objective relative tolerance1e-5, initial sigma² derived from mean
pair distance, sigma² floor1e-10 normalized units², E-step tolerance1e-6, scale estimation
on, reflection off, gamma1, kappa infinity, posterior variance terms off, no subsampling,
default FPS from point0, landmark seed equal to fixture seed, no external evaluator,
no cancellation, stage/iteration observers enabled. All arms share these settings.

Fixture generator is the retained native runner's Make plus
`benchmarks/runners/CoherentPointDriftScalingFixture.hpp::Surface`. Source is the seeded
bumpy ellipsoid; truth is the known bend followed by scale1.1, rotation.3 and translation
(.3,-.2,.1). Target has .004-standard-deviation coordinate noise and every20th row is
uniform clutter. Partial omits nonclutter targets with unrounded truth.x>=.2.
`fixtures/{full,partial}_{1,2,3}.npz` preserve full native `--dump 100000` outputs as float32
arrays; accompanying previews preserve first/last3 raw text points. The existing dump prints
six significant digits; all coordinate magnitudes are <10, giving a vector rounding budget
<1e-5 world units including float32 storage. The runner internally regenerates its full
precision fixture and uses its own truth for native RMS. Python verifies recomputed RMS
within1e-5. Partial subset membership within1e-5 of x=.2 is counted separately.

Measurement owners: `load_cell`, `measure`, `summarize`, `report`. Reused METHOD-081
`traces`, `distances`, `quality`; current experiment has its own termination/report logic.
Endpoint positions are little-endian packed double3 (N*24 bytes), checked finite and exact
shape. Native exit1 with a failure status is retained without requiring positions; success
without valid positions is technical invalidity. Final convergence-check E-step emits no
iteration record: the engine's returned termination is authoritative, trace contains only
completed M-steps. NLL/objective/mass precede update; sigma² follows update. Actual policy
enum:0Reference,1Dense,2Truncated,3Auto(request),4FastGauss,5Nyström,6Vulkan.

Per native process timeout900s, entire execution/intercell reporting budget31200s (34 native budgets plus600s report overhead) enforced
by timeout and SIGALRM. Final/failure HTML after alarm cancellation is outside this budget.
Child address-space limit3GiB; cumulative child high-water RSS retained, not per-cell RSS.
A native timeout marks that cell unevaluated and subsequent fixed cells continue; exhausted
total budget leaves remaining cells unevaluated. A changed digest, process failure other
than explicit native result, missing/nonfinite/malformed successful output or inconsistent
trace stops execution and retains a partial report. Scientific nonconvergence, sigma floor,
numerical failure and endpoint differences do not cancel later cells. No adaptive parameters,
extra seeds or iterations, automatic algorithm fixes or reruns.

Each process is fresh; no warmup. Native registration wall seconds include trace observers
but exclude fixture generation, process startup, file writing and Python analysis. Timings
are descriptive; no timing verdict or performance ranking. Hardware/python/library versions
are recorded in `environment.json`. Technical final-config checks for candidate FPS/full+partial
are pass/fail only; their numerical results are neither inspected nor used as scientific data.

Run retains JSON, positions, stdout, stderr trace, argv, outcomes, summary, per-cell PNGs and
an HTML report. Freeze after source/task/packet commit. Generate manifest.txt with the exact
argv returned by `convergence.py::manifest_command()`; this includes tracked/untracked
inputs, six fixture archives, scripts, both binaries, LayerA/B and verification evidence,
excluding mutable preflight/reviews/runs/verification (selected evidence added explicitly).
Packet digest is SHA256(manifest.txt). Before every start reproduce that digest and require
two truthful PASS records at reviews/approval.json. No source/commit change during execution.
