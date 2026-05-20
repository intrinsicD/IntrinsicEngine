# Method Implementation Review Checklist

Use this checklist when reviewing method/paper implementation tasks.

## Paper claim and formulation

- [ ] Paper claim is captured correctly (objective, assumptions, expected output).
- [ ] Mathematical formulation is explicit (objective/constraints/diagnostics).
- [ ] Input/output contract and units are explicit.

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
