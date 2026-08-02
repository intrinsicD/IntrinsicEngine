# Method Workflow

This workflow governs scientific paper/method implementation in IntrinsicEngine.

## Required sequence

1. **Paper intake**
   - Query and review the original paper plus relevant extensions and
     subsequent improvements; record stable citations and distinguish the
     formulation actually implemented from later variants.
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

## Required engine-integration matrix

Every new or materially changed method task declares
`method.engine-integration` and includes `## Engine integration`. The matrix
must dispose these fields even when the current slice is method-only:

- **Least-structured input:** weakest data/topology contract the kernel needs.
- **Compatible entity sources:** all ECS sources satisfying that contract.
- **RuntimeModule:** runtime binding/availability owner.
- **Config/agent:** shared validated non-UI control path.
- **UI:** every appropriate geometric-domain panel and readiness state.
- **Publication:** destination property/entity and topology/cardinality policy.
- **End-to-end tests:** source-domain-to-publication/UI coverage.

A deferred field names its follow-up task. `N/A` requires a method-contract
reason; method-only scope is not by itself a reason. Geometry methods also
apply `geometry.element-domain-sources`: a point/vertex-span kernel consumes
mesh vertices, graph nodes, and point-cloud points without a converter.

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
