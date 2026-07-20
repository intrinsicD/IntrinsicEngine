---
id: METHOD-036
theme: I
depends_on: [METHOD-032, METHOD-034, METHOD-035]
---
# METHOD-036 — Normal-orientation method comparison evidence (publication protocol)

## Goal
- Produce the publication-grade, shared-protocol comparison of point-cloud normal-orientation methods: octree parity (`METHOD-032`), the in-repo MST baseline (`Geometry.PointCloud.Normals`), iPSR (`METHOD-034`), and PGR (`METHOD-035`) — identical fixtures, identical unoriented input normals, identical scramble seeds, per-method `benchmark_id`s — yielding a results report and the measured tables for the `METHOD-032` `paper.md`.

## Non-goals
- No neural baselines (dipole propagation, learned orientation networks): the engine has no ML runtime, and training-dependent results are not reproducible under this repository's determinism policy. This exclusion is recorded in the report so the competitor set is honest about its scope.
- No new algorithm implementations and no changes to the compared methods — measurement and reporting only.
- No performance or quality claims outside the measured data; no claims enter `README`/`paper.md`/task closures before the results audit.

## Context
- Comparison protocol: every method consumes the *same* precomputed unoriented normals per fixture (estimation runs once with `OrientationMode::None`), so the comparison isolates orientation quality from estimation quality. Seeds (fixture sampling, sign scramble, iPSR init) are pinned in the manifests.
- Fixture matrix: the `METHOD-032` synthetic suite (sphere, torus, thin plate, hollow shell, open hemisphere, noise ladder) for the smoke lane; larger declared scan datasets for the heavy/nightly lane per `docs/methods/dataset-policy.md` — no external data in smoke.
- Metrics: `oriented_correct_fraction` (primary), per-method diagnostics (parity conflict rate, iPSR iterations, PGR residual), `runtime_ms` (reported, never the headline), and per-fixture failure statuses (a method that fails closed on the open hemisphere is *correct*, and the report says so).
- Report lands under `methods/geometry/octree_parity_orientation/reports/` following `docs/methods/report-template.md`; audited per the results-audit checklist before any claim propagates.

## Required changes
- [ ] Shared benchmark manifest family under `benchmarks/geometry/manifests/` with stable per-method `benchmark_id`s over the identical fixture/seed matrix (smoke lane). The shared metric names must already be schema-valid — the owning method tasks (`METHOD-032`/`033`/`034`/`035`) extend `ALLOWED_METRICS` when their manifests land; verify with `tools/benchmark/validate_benchmark_manifests.py` before measuring and extend the schema here only for any comparison-specific metric this task introduces itself.
- [ ] Heavy/nightly manifests with declared datasets per `docs/methods/dataset-policy.md`.
- [ ] Comparability guard: a validation step (test or benchmark preflight) asserting all compared runs consumed bitwise-identical fixture inputs and unoriented normals.
- [ ] Comparison report in `methods/geometry/octree_parity_orientation/reports/` with per-method, per-fixture tables and failure-status accounting.
- [ ] Measured tables and competitor discussion folded into the `METHOD-032` package `paper.md`.

## Tests
- [ ] Benchmark result payloads validate (`tools/benchmark/validate_benchmark_results.py`) for every compared method.
- [ ] The comparability guard fails when any method's input fixture or unoriented normals diverge.

## Docs
- [ ] Report document (per template) with limitations and the neural-baseline exclusion rationale.
- [ ] `METHOD-032` package `README.md`/`paper.md` updated with measured results only.

## Acceptance criteria
- [ ] All four methods measured on the full smoke matrix with pinned seeds; heavy lane executed at least once with declared datasets.
- [ ] Report written, and the results audit (adversarial referee pass) is completed before closure — findings resolved or explicitly recorded.
- [ ] No unmeasured or extrapolated claims anywhere in the touched docs.

## Verification
```bash
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No edits to the compared method implementations (fixes go through their owning tasks).
- No dataset additions outside `docs/methods/dataset-policy.md`; no external data in the smoke lane.
- No selective reporting: every configured fixture appears in the report, including failures.

## Maturity
- Evidence task: it produces audited measurements, not an engine capability, so no maturity level is claimed and no `Operational` follow-up is owed. Do not close before the results audit has run.
