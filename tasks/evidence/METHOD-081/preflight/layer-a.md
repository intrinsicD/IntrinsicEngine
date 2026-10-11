# Execution specification

Repository: /home/alex/Documents/IntrinsicEngine. CPU-only local diagnostic, identifier
`method081_exact_row_repair_prefix`; native outputs are the existing diagnostic runner's
JSON format, not schema-v2 performance benchmark results. No claim-eligible performance ranking.
Review composition: two fresh independent Codex reviewers. The operator explicitly authorized
this substitution after Claude's weekly quota prevented its review ("Ok ohne Claude", 2026-10-11).
The start gate requires truthful `codex_a` and `codex_b` PASS records on the same digest.

Sources: baseline engine is commit a93dcb8c7; candidate is the current frozen engine.
`../reviews/code.patch` shows the four implementation/test/doc changes. The runner in both
binaries is `../diagnostic-runner.cpp`, adding --iterations/--trace to the existing runner.
`../binary-bindings.json` binds source and executable SHA256s, build commands and environment.
The baseline build preceded production edits; the candidate build followed restoration after
mutation tests. Both use the ci-release preset, target IntrinsicCoherentPointDriftLandmarkVariants.
Private CPU E-step code differs; all other compiled algorithm source is identical.

Invocation from the repository root:
```
OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 python3 tasks/evidence/METHOD-081/compare.py run
```
`compare.py::cells`, `command`, `read`, `report` own the complete plan/measurement.
Ten serial cells in this exact order:
1. exact_7 (baseline, Auto, 7 iterations)
2. exact_2 (baseline, Auto, 2 iterations)
3. baseline_points_1 (7)
4. baseline_box_1 (2)
5. candidate_points_1 (7)
6. candidate_box_1 (2)
7. candidate_box_2 (2)
8. candidate_points_2 (7)
9. baseline_box_2 (2)
10. baseline_points_2 (7)

Common algorithm configuration: runner fixture full, seed 1, 100000 source/target points,
8 threads pinned to CPUs 0-7, initial landmarks 256, sampled error limit .001. Points means
points/fixed/random: FPS first index 0; the random suffix is an unused box-pattern choice.
Box means box/resampled/random: current sigma-padded union boxes; seed follows draw.
The literal runner assignments resolve the remaining parameters: Bayesian variant,
normalized inputs, outlier weight .1, beta 1, lambda 2, low rank 100; no subsampling.
`command()` emits every native argument, including the matching exact_7/2 position reference.
Each process starts fresh without warmup. Timing is the runner's registration-only wall clock;
fixture generation, output writing, Python analysis and process startup are excluded.

Native cell timeout 240 seconds; total driver budget 1200 seconds including reports between
cells, enforced by SIGALRM and checked before each start; child address-space limit 3 GiB. No changed configuration,
seed, repeat count or adaptive cell is permitted. A technical failure/timeout leaves the
comparison unevaluated. Completed outputs persist. Scientific discrepancies do not cancel
remaining cells. Peak child RSS is recorded as a cumulative high-water mark, not per-cell RSS.
`technical.json` records final-size pass/fail checks; numeric method outcomes from those checks
are neither read nor reported as scientific results. `fixture-preview.txt` is only a display subset.

Raw endpoint positions, native stderr iteration/stage traces, commands, JSON and resource logs
are retained under ../runs. Reports plot the exposed NLL/stopping objective, sigma², sampled
error, actual E-step policy and stage transitions; before/after displays use fixed first/last
100-point subsets. The full 100000 positions are used in endpoint comparisons.

Freeze: after final source/packet commit, execute the exact argv from
`compare.py::manifest_command()` and redirect bytes to preflight/manifest.txt. It hashes the
whole tracked/untracked source tree except mutable packet/review/run/verification directories;
explicit extras restore the two packet layers, both binaries, mutation script/log, focused
test log and synthetic report self-check/script. The runner recomputes the same manifest and checks the approved digest before every
cell. Reviews are recorded outside the manifest. No commit or source edit during execution.
