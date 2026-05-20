# Method Workflow

This workflow governs scientific paper/method implementation in IntrinsicEngine.

## Required sequence

1. **Paper intake**
   - Capture claims, assumptions, and required inputs/outputs.
   - Define method contract and failure modes.
2. **CPU reference backend first**
   - Implement deterministic, correctness-first baseline.
3. **Correctness tests**
   - Add analytic/simple-case and regression tests.
4. **Benchmark harness/manifests**
   - Add reproducible benchmark IDs, dataset references, and metrics.
5. **Optimized CPU backend**
   - Compare numerics and diagnostics against reference backend.
6. **GPU backend (optional, later)**
   - Add only after CPU reference parity is established.
7. **Limitations and diagnostics**
   - Document degenerate-input behavior and numerical limitations.

## Backend policy summary

- Reference backend is the canonical truth for correctness.
- Optimized/GPU backends must report backend identity and parity deltas.
- Backend differences must be measurable and documented.

## Verification expectations

- Method correctness tests pass.
- Benchmark manifests validate.
- Benchmark outputs include machine-readable diagnostics and status.

## Required references

- [Methods docs index](../methods/index.md)
- [Reference implementation policy](../methods/reference-implementation-policy.md)
- [Backend policy](../methods/backend-policy.md)
- [Numerical robustness policy](../methods/numerical-robustness-policy.md)
- [Dataset policy](../methods/dataset-policy.md)
- [Method report template](../methods/report-template.md)
- [Method implementation review checklist](./method-review-checklist.md)
