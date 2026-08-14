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
apply `geometry.element-domain-sources`: a point-set kernel consumes any
compatible typed property/span on any resolved mesh, graph, or point-cloud
element domain without a converter or handle-specific property requirement. A
graph kernel adds only its named adjacency/connectivity sources and therefore
also accepts meshes satisfying them. Same-cardinality results publish named
properties back to the originating domain; topology/cardinality edits remain
explicit owning operations.

Engine bindings express each method input and output as a semantic slot backed
by a full canonical property reference: element domain, original property name,
and value kind. Slot semantics never prescribe a storage name. A `Position`
slot may therefore bind `f:centroid`, `e:sample`, or another compatible property
without copying or aliasing it to `v:position`; the runtime resolver validates
the property and passes its typed property/span to the kernel. Paired slots add
their actual count/domain/correspondence requirements, and topology-aware slots
add only the adjacency sources the method really consumes.

Public and persisted geometry vector properties use float `glm::vec*` storage.
Precision-sensitive kernels may promote values to `double`/`glm::dvec*` for
internal computation, then convert at the result-publication boundary. Do not
publish `glm::dvec*` properties by default or add silent persistent float/double
alias properties. A method that genuinely requires a public double-vector
contract must declare that exception, add typed catalog support, and test every
control surface explicitly.

## Backend policy summary

- Reference backend is the canonical truth for correctness.
- Optimized/GPU backends must report backend identity and parity deltas.
- Backend differences must be measurable and documented.

## Verification expectations

- Method correctness tests pass.
- Benchmark manifests validate.
- Benchmark outputs include machine-readable diagnostics and status.

## Review checklist

Apply when reviewing method/paper work before commit or PR.

**Paper claim and formulation** — the claim is captured correctly (objective,
assumptions, expected output); the mathematical formulation, input/output
contract, and units are explicit; the literature pass covers the original
paper plus relevant extensions and names which formulation is implemented.

**Engine integration** — the task declares `method.engine-integration` and
every applicable catalog contract; the least-structured input is explicit and
runtime/UI accept every ECS source satisfying it; point-set eligibility is
typed properties/spans on any element domain (never `Vertices`, point-cloud
provenance, or handle-specific wrappers); graph eligibility adds only named
adjacency and accepts satisfying meshes; RuntimeModule, config/agent, UI,
publication/cardinality, and end-to-end test rows are implemented, contractually
inapplicable, or owned by named follow-ups; UI discovery covers each
appropriate domain through the same readiness path as non-UI controls;
same-cardinality results update only named properties on the originating
domain and never silently discard richer source data.

**Robustness and correctness** — degenerate/boundary cases defined and
handled; the CPU reference backend exists and is the correctness baseline;
correctness tests include analytic/simple cases and regressions; tolerances
and acceptance criteria are documented.

**Benchmarking and parity** — a benchmark manifest exists for the method
scope; quality metrics are defined (not runtime-only); optimized CPU and GPU
backends are compared against reference outputs.

**Diagnostics and docs** — results report diagnostics and backend identity;
failure modes are explicit and actionable; known limitations are documented;
`methods/**` / `docs/methods/**` updated for touched behavior; the task has
acceptance criteria and verification commands; the PR links the
benchmarks/tests used for validation.

## Required references

- [Methods docs index](../methods/index.md)
- [Reference implementation policy](../methods/reference-implementation-policy.md)
- [Backend policy](../methods/backend-policy.md)
- [Numerical robustness policy](../methods/numerical-robustness-policy.md)
- [Dataset policy](../methods/dataset-policy.md)
- [Method report template](../methods/report-template.md)
