---
name: intrinsicengine-method
description: Scientific paper/method implementation workflow for IntrinsicEngine. Defines the required CPU-reference-first sequence (paper intake → CPU reference backend → correctness tests → benchmark harness → optimized CPU backend → optional GPU backend → limitations docs), the backend policy where the reference is canonical truth and optimized/GPU backends must report backend identity and parity deltas, and the method-implementation review checklist (paper claim capture, robustness, backend parity, diagnostics, docs). Use this skill whenever the user is implementing a paper or research method, touching anything under `methods/` or `docs/methods/`, deciding between reference and optimized backends, adding correctness tests for a paper-backed algorithm, reviewing a method PR, or whenever the user mentions a paper title, "reference backend", "parity", or "method contract".
---

# IntrinsicEngine Method Implementation Workflow

This skill governs scientific paper/method implementation in IntrinsicEngine.
Method code lives under `methods/` and `docs/methods/`; the engine's broader
layering rules in `intrinsicengine-core` constrain how `methods/` integrates
with other layers.

The core invariant: **the CPU reference backend is the canonical truth for
correctness**. Optimized and GPU backends are validated *against* the reference,
never against each other.

## Required sequence

Implement method work in this order. Earlier steps gate later ones; do not skip.

1. **Paper intake.** Capture claims, assumptions, and required inputs/outputs.
   Define the method contract and failure modes. The contract names objective,
   constraints, diagnostics, and the input/output shape with units.

2. **CPU reference backend first.** Implement a deterministic, correctness-first
   baseline. No SIMD, no parallelism, no GPU — clarity over speed. This becomes
   the parity oracle for every subsequent backend.

3. **Correctness tests.** Add analytic/simple-case tests (closed-form inputs
   with known outputs) and regression tests. Label them appropriately per
   `intrinsicengine-core` test categories.

4. **Benchmark harness and manifests.** Add reproducible `benchmark_id` values,
   dataset references, and metric definitions. See `intrinsicengine-benchmark`
   for the manifest format and runner requirements.

5. **Optimized CPU backend.** Compare numerics and diagnostics against the
   reference. The optimized backend must report its identity and parity deltas;
   any divergence beyond declared tolerances is a regression, not a feature.

6. **GPU backend (optional, later).** Add only **after** CPU reference parity
   is established. The same parity-delta rules apply — GPU outputs are compared
   against the reference, not against the optimized CPU backend.

7. **Limitations and diagnostics.** Document degenerate-input behavior,
   numerical limitations, and failure modes. Method docs must describe current
   behavior, not aspirational behavior.

For the full source procedure, read `references/method-workflow.md`.

## Backend policy

- **Reference backend = canonical truth for correctness.** Treat its output as
  the ground truth for every comparison.
- **Optimized/GPU backends must report:**
  - backend identity (which implementation produced this result),
  - parity delta against the reference (numerical difference + how it was
    measured).
- **Backend differences must be measurable and documented.** Silent divergence
  is the failure mode this policy is designed to prevent.

## Verification expectations

- Method correctness tests pass under the default CPU gate.
- Benchmark manifests validate with
  `tools/benchmark/validate_benchmark_manifests.py`.
- Benchmark outputs include machine-readable diagnostics and status.

## Method-implementation review checklist

Apply this checklist when reviewing any method/paper-implementation task. Read
`references/method-review-checklist.md` for the full version.

### Paper claim and formulation

- Paper claim is captured correctly: objective, assumptions, expected output.
- Mathematical formulation is explicit (objective / constraints / diagnostics).
- Input/output contract and units are explicit.

### Robustness and correctness

- Degenerate and boundary cases are defined and handled.
- CPU reference backend exists and is treated as the correctness baseline.
- Correctness tests include simple/analytic cases and regression coverage.
- Numerical tolerances and acceptance criteria are documented.

### Benchmarking and backend parity

- Benchmark manifest exists for the method scope.
- Quality metrics are defined (**not runtime-only** — a method that's fast and
  wrong is worse than a method that's slow and right).
- Optimized CPU backend is compared against reference outputs.
- GPU backend (if present) is compared against reference outputs.

### Result quality and diagnostics

- Method result includes diagnostics and backend identity.
- Failure modes and status reporting are explicit and actionable.
- Known limitations are documented in method docs/report.

### Documentation and process

- Method docs updated (`methods/**`, `docs/methods/**`) for touched behavior.
- Task file includes acceptance criteria and verification commands (see
  `intrinsicengine-task-workflow`).
- PR includes links to benchmarks/tests used for validation.

## How method work maps to the maturity taxonomy

The CPU-reference-first flow is the canonical
`Scaffolded → CPUContracted → Operational → ParityProven` sequence for method
work:

- **Scaffolded** — method contract and module structure exist; reference
  backend may be a stub.
- **CPUContracted** — reference backend implemented; analytic and regression
  tests pass in the default CPU gate.
- **Operational** — optimized CPU and/or GPU backend implemented with
  parity-delta reporting against the reference.
- **ParityProven** — backend deltas are within documented tolerances on the
  canonical dataset; benchmark report cites baseline comparison.

See `intrinsicengine-task-workflow` for the full taxonomy and closure rules.

## Related repo documentation

- `methods/README.md` — package structure and manifest expectations.
- `docs/methods/index.md` — methods documentation index.
- `docs/methods/reference-implementation-policy.md`
- `docs/methods/backend-policy.md`
- `docs/methods/numerical-robustness-policy.md`
- `docs/methods/dataset-policy.md`
- `docs/methods/report-template.md`

## References

- `references/method-workflow.md` — required sequence, backend policy,
  verification expectations.
- `references/method-review-checklist.md` — full review checklist with all
  rows; use this for any method-PR review.
