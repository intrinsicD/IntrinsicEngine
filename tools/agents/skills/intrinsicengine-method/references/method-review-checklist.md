# Method Implementation Review Checklist

Use this checklist when reviewing method/paper implementation tasks.

## Paper claim and formulation

- [ ] Paper claim is captured correctly (objective, assumptions, expected output).
- [ ] Mathematical formulation is explicit (objective/constraints/diagnostics).
- [ ] Input/output contract and units are explicit.
- [ ] Literature review covers the original paper and relevant extensions or
      improvements, and identifies which formulation is implemented.

## Engine integration

- [ ] The task declares `method.engine-integration` plus every applicable
      domain contract from the contract catalog.
- [ ] The least-structured input is explicit; runtime and UI accept every ECS
      source satisfying it rather than gating on narrower provenance.
- [ ] `RuntimeModule`, config/agent, UI, publication/cardinality, and end-to-end
      test rows are implemented, explicitly inapplicable by contract, or owned
      by named follow-up tasks.
- [ ] UI discovery covers each appropriate geometric domain and uses the same
      runtime availability/readiness path as non-UI controls.
- [ ] Same-cardinality results publish to the originating element domain;
      topology/cardinality changes are explicit owning operations and do not
      silently discard richer source data.

## Robustness and correctness

- [ ] Degenerate and boundary cases are defined and handled.
- [ ] CPU reference backend exists and is treated as correctness baseline.
- [ ] Correctness tests include simple/analytic cases and regression coverage.
- [ ] Numerical tolerances and acceptance criteria are documented.

## Benchmarking and backend parity

- [ ] Benchmark manifest exists for the method scope.
- [ ] Quality metrics are defined (not runtime-only).
- [ ] Optimized CPU backend is compared against reference outputs.
- [ ] GPU backend (if present) is compared against reference outputs.

## Result quality and diagnostics

- [ ] Method result includes diagnostics and backend identity.
- [ ] Failure modes and status reporting are explicit and actionable.
- [ ] Known limitations are documented in method docs/report.

## Documentation and process

- [ ] Method docs were updated (`methods/**`, `docs/methods/**`) for touched behavior.
- [ ] Task file includes acceptance criteria and verification commands.
- [ ] PR includes links to benchmarks/tests used for validation.
