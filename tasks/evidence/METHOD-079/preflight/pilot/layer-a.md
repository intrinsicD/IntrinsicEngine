# Layer A — execution reconstruction

Read only this layer, binary-bindings.json, the named implementation and AGENTS.md until Layer B is supplied. Do not read intent/task/plan/addendum, previous run results, technical outputs, Layer B or other review answers. Read only; no delegation. Reconstruct measurements before assessing intent.

## Frozen objects
Base production commit: 0631ef12c (same E-step/runner as bbdf1af79). Frozen execution revision is line 1 of manifest.txt. Temporary changes are limited to src/geometry/Geometry.Registration.CoherentPointDrift.EStep.cpp and benchmarks/runners/CoherentPointDriftLandmarkVariantsRunner.cpp; inspect their diff with `git diff --no-ext-diff --no-textconv 0631ef12c -- <these paths>`.

Executable `native` at /tmp/cpd-nystrom-row-20261011/bin is built by `cmake --build --preset ci-release --target IntrinsicCoherentPointDriftLandmarkVariants`. `analyze` is a standalone C++23/Eigen executable from tasks/evidence/METHOD-079/analyze.cpp, built with the exact command in binary-bindings.json. No engine/public module or test target was added for it. That file binds sources, binaries and Python package versions. No build occurs during execution.

## Exact plan and resolved configuration
From /home/alex/Documents/IntrinsicEngine:
```
python3 tasks/evidence/METHOD-079/diagnose.py run
```
`diagnose.py plan` prints every native argv. PLAN, native_command and analysis_command own all starts: points_1 (points/fixed/random, 4 iterations), box_1 (box/resampled/random, 2), box_2 (same, 2), points_2 (same, 4). Each native call is followed by `taskset -c 0-7 /tmp/cpd-nystrom-row-20261011/bin/analyze <cell>.snapshot <cell-prefix>` if a snapshot exists. No adaptive cells, variant changes or extra seeds.

All cells: synthetic full fixture; 100000 source and target points; seed 1; 8 threads pinned to cores 0-7; Bayesian variant; NormalizeInputs=true; OutlierWeight=.1; Beta=1; Lambda=2; LowRank=100; initial sigma² automatic (0); floor=1e-10; stopping tolerance=1e-5; EStepTolerance=1e-6; estimate scale=true; posterior variance terms=false; no source/target subsampling. Nystrom landmarks start at 256, error limit .001; points use default farthest-point sampling, boxes use the named random pattern. All other Params defaults and fixture generation source are frozen by the manifest. Nyström eigen cutoff (1e-10 relative), doubling/cost limit and rejection memory are unchanged.

INTRINSIC_NYSTROM_CAPTURE names the cell's new snapshot path. At the first target-pass failure, CAS-min selects the lowest invalid target index over chunks; the in-pass value and same-order recomputation must match bitwise. Each evaluator writes at most one snapshot. Target/source validity, sampled-error and cost failures remain logged. Empty landmarks, eigensolver failure and invalid spectral maximum share a generic rejected-attempt record and remain unresolved without a snapshot. Each attempted Gram eigensystem emits draw, L, internal sigma², retained rank/top/smallest eigenvalue, and the indices/distance-over-sigma of the two largest absolute coefficients of its smallest retained mode.

## Data layout and transformations
Snapshot: 5 native little-endian uint64 values (79, L, M, target index, draw); 7 doubles (internal sigma², logC, LogWeightShift, target xyz, in-pass shifted sum); landmark XYZ arrays; moved-source XYZ and shifted source log-weights; native column-major Gram; eigenvalues; column-major eigenvectors; retained mask; target kernel row; aggregate a; g; column-major pseudoinverse. The offline parser bounds sizes, checks complete consumption and uses full-fixture N=M.

analyze.cpp emits a TSV and source-contribution binary (per source: exact shifted log affinity, rebuilt signed affinity, distance to nearest landmark / sigma), spectrum CSV, component CSV and top-eight exact-source-contributor CSV. Native scalar row replay and native P*a replay must match bitwise. Extended precision is host long double, with mantissa digits recorded. Separate paths use saved factors, re-diagonalized native Gram, and rebuilt standard Gaussian kernels with both matched native retained rank and their own relative cutoff. No upstream geometry precision is restored. Exact row sums use long-double log-sum-exp. Counts, signed values, spectrum, kernel zeros, projection residuals and outlier/inlier quantities are emitted by the same source. Plotting contribution arrays are float64; raw long-double aggregate text is preserved.

The fixture's full target indices divisible by 20 are injected clutter. Only the selected row is classified; no invalid-row census. Each native JSON and endpoint binary is also retained. Geometry.Registration.CoherentPointDrift.cpp (Coherence and Step) is in scope for reviewing trace semantics. Native stage/iteration traces are parsed by the existing frozen METHOD-078 traces() helper; no old result file or old verdict is used. The static first/last-64 fixture preview is reused solely for before/after plots. diagnose.py owns numerical interpretation, repeat equality, plots and HTML. Its rules and units are visible in code.

## Resource, failure and provenance rules
New exclusive output directory tasks/evidence/METHOD-079/runs. Optimized Python execution is explicitly refused before importing the driver dependencies. Both passing review records must bind the packet digest. Before EACH native/analysis start, the driver reconstructs the live manifest and matches frozen bytes. Native wall bound 90s; offline bound 120s; each child 8-GiB address-space cap; complete plan 900s via process alarm. SIGTERM follows the same failure path. Current child is killed by subprocess.run on exception/timeout. Failed execution stops the remaining plan; scientific negatives do not. Snapshot absence is recorded unresolved and does not authorize another cell. No source/binary edits during execution.

Failure.json precedes recovery HTML. Per-phase exit records gate cell verdicts. Reports list unrun cells and whether overall completion was recorded; successful early convergence is accepted within the configured maximum. HTML starts with an incomplete/no-verdict skeleton before plot assembly; a rendering failure cannot imply completion. Recovery reporting occurs after alarm cancellation. Maximum completed-child RSS is recorded cumulatively in KiB, not claimed per-cell. There is no runtime comparison. Native JSON seconds includes probes and has no performance interpretation.

## Manifest command
```
/usr/bin/python3 /home/alex/.codex/skills/experiment-preflight/manifest.py --exclude tasks/evidence/METHOD-079/preflight/pilot --exclude tasks/evidence/METHOD-079/reviews --exclude tasks/evidence/METHOD-079/runs /tmp/cpd-nystrom-row-20261011/bin tasks/evidence/METHOD-079/preflight/pilot/layer-a.md tasks/evidence/METHOD-079/preflight/pilot/layer-b.md
```
Commit packet, code and task state first; then store this command's output as manifest.txt. Packet digest = SHA256 of manifest.txt. Reviews, manifest and run outputs are outside the digest. No other changes after freeze.
