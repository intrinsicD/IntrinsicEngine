---
id: METHOD-034
theme: I
depends_on: [METHOD-033]
maturity_target: CPUContracted
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
---
# METHOD-034 — iPSR normal orientation baseline (reference backend)

## Goal
- Add a CPU reference implementation of Iterative Poisson Surface Reconstruction (iPSR) as a modern competitor baseline for the `METHOD-032` publication track: initialize normals with seeded random directions, then iterate — screened Poisson reconstruction from the current normals, transfer of reconstructed-surface normals back to the input points via nearest faces — until the per-iteration flip fraction drops below a threshold or the iteration cap is reached. Output oriented normals plus convergence diagnostics.

## Non-goals
- Not a replacement for the engine's default orientation paths — this is comparison infrastructure; engine-facing selection stays out of scope.
- No optimized CPU or GPU backend; no neural components.
- No separate reconstruction implementation. Consume METHOD-033's public API
  only after its shared iterative-input contract has been frozen and tested;
  do not bypass validation or depend on private solver state.
- The cross-method comparison protocol and report are owned by `METHOD-036`, not this task.

## Context
- Paper/method: Hou, Wang, Bao, et al. — "Iterative Poisson Surface Reconstruction (iPSR) for Unoriented Points", SIGGRAPH 2022.
- Method package: `methods/geometry/ipsr/`; implementation is package-local (`include/` + `src/`, the `progressive_poisson` pattern) — a research baseline does not warrant a `src/geometry` module surface.
- Reuse: `Geometry.SurfaceReconstruction.Poisson` (`METHOD-033`) for the inner solve; `Geometry.KDTree` for point-to-face normal transfer; `Geometry.PointCloud.SurfaceSampling` for fixtures.
- Seeding: iPSR legitimately requires an RNG for the initial normals; the seed is an explicit param, and the `METHOD-036` comparison protocol pins it.
- Diagnostics use the same orientation-correctness definition as METHOD-032,
  plus iterations and final flip fraction. Shared names alone do not make
  input information or timing budgets comparable; METHOD-036 owns that protocol.
- Operator decision (2026-09-05): reconcile the random initial directions
  with METHOD-033 during joint primary-source intake, before implementation.
  This baseline is not an orientation-only consumer of precomputed normals;
  METHOD-036 must disclose that information difference and compare it in the
  joint estimation/reconstruction group.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Finite position properties/spans on any element domain; explicit seed and iteration/solver params. Initialization does not consume a supplied normal field. |
| Compatible entity sources | Any mesh, graph, or point-cloud property domain satisfying the position contract; benchmark arrays carry no provenance gate. No ECS adapter is part of this comparison-only contract. |
| RuntimeModule | No production selector is intended; METHOD-036 owns the programmatic benchmark consumer, not runtime promotion. |
| Config/agent | Explicit params/seeds are driven by METHOD-036 manifests; no separate engine config state or private UI path. |
| UI | Not applicable to the deliberately comparison-only baseline; any later production promotion requires a separate task. |
| Publication | Return float normals with unchanged input cardinality/order, plus status/diagnostics; do not mutate ECS or source topology. METHOD-036 stores result evidence only. |
| End-to-end tests | METHOD-036 owns benchmark invocation/input-identity/result-validation coverage; this task owns CPU convergence, determinism, and failure controls. No editor-integration claim is owed. |

## Required changes
- [ ] Complete primary-source intake with METHOD-033, confirming the inner
      formulation and intermediate-input contract before coding. Record any
      uniform-grid simplification and its fidelity limits instead of labeling
      an unvalidated variant as exact paper parity.
- [ ] Clone `methods/_template/` to `methods/geometry/ipsr/`; fill `method.yaml` (`id: geometry.ipsr`; status `reference`; metrics above; paper block) and `paper.md`.
- [ ] Package-local reference implementation: params (seed, max iterations, flip-fraction convergence threshold, inner Poisson params passthrough), result (oriented normals, per-iteration flip-fraction history, converged flag, status), diagnostics assembly.
- [ ] Deterministic given `(input, params, seed)`: fixed iteration order and transfer tie-breaking; bitwise-identical outputs across runs.
- [ ] Fail-closed with explicit statuses: empty/too-small input, non-finite data, inner reconstruction failure (propagated, not swallowed), iteration cap reached without convergence (distinct non-success status).
- [ ] Smoke benchmark manifest using existing permitted metrics with frozen
      error formulas; keep `oriented_correct_fraction`, iterations, and
      final flip fraction as structured diagnostics unless a separate schema
      decision explicitly admits a new gating metric.

## Tests
- [ ] `tests/unit/geometry/Test.IPSROrientationBaseline.cpp` with `unit;geometry` labels.
- [ ] Sphere and torus position fixtures with seeded random normal
      initialization reach a documented `oriented_correct_fraction` bound
      within the iteration cap; ground-truth normals are scoring-only.
- [ ] The first iteration consumes METHOD-033's contracted inconsistent-normal
      input path; genuine inner failures propagate without false convergence.
- [ ] Determinism given a fixed seed; cap-without-convergence returns its distinct status; fail-closed cases above.

## Docs
- [ ] `methods/geometry/ipsr/README.md` — parameter guidance (seed, iteration cap, inner grid resolution) and known limitations (cost per iteration, sensitivity to inner reconstruction quality on sparse/noisy input).

## Acceptance criteria
- [ ] Reference implementation present and tested in the default CPU gate.
- [ ] `method.yaml` validates; benchmark smoke manifest validates and runs with quality metrics shared with `METHOD-032`.
- [ ] Public surface type discipline: `std`/`glm`/scalar plus engine point-cloud types; nothing method-internal exported.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'IPSR' --timeout 300
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No private solver access or unilateral changes to METHOD-033 after the
  shared contract is frozen; necessary revisions go through its owning task
  and update both contracts/tests together.
- No optimized/GPU work; no performance claims without baseline comparison.
- No external datasets in smoke tests.

## Maturity
- Target: `CPUContracted` (baseline-grade reference for the comparison protocol).
- No `Operational` follow-up is owed — this baseline exists for `METHOD-036` evidence, not for engine promotion.
