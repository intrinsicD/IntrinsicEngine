# Method API Contract

This document defines the canonical C++ API shape for method packages integrated into IntrinsicEngine.

## Scope and ownership

- Method packages live under `methods/<domain>/<method-id>/`.
- Production engine integration code belongs in owning runtime/graphics/geometry layers; method package code remains method-focused and backend-agnostic at the public contract layer.
- Public method APIs must not expose internal engine wiring details.

## Canonical API shape

Method packages should expose one contract with stable semantic types:

```cpp
struct Params;
struct Input;
struct Result;
struct Diagnostics;

auto Execute(const Input&, const Params&) -> Expected<Result, MethodError>;
auto ExecuteGpu(const Input&, const Params&, GpuContext&) -> Expected<Result, MethodError>;
```

`Execute` is the CPU entry point (reference or optimized CPU selection by policy/configuration).
`ExecuteGpu` is optional and is introduced only after CPU reference parity is established.

## Required result payload

Every method result must encode payload + diagnostics + backend provenance:

```cpp
struct Result {
    Payload payload;
    Diagnostics diagnostics;
    MethodBackend actual_backend;
    MethodStatus status;
};
```

Required semantics:

- `payload`: the method output data (e.g., fields, meshes, transforms).
- `diagnostics`: numerical stability counters, iteration counts, residual/error values, and degeneration handling metadata.
- `actual_backend`: backend that actually executed (`cpu_reference`, `cpu_optimized`, `gpu_*`, etc.).
- `status`: success/degraded/fallback status used by callers and tests.

## Backend progression policy

Backends are promoted in this order:

1. `cpu_reference`
2. `cpu_optimized`
3. `gpu_*` (only after reference parity)

This enables mathematically reliable validation before performance specialization.

## Deterministic testing requirements

Method tests must support deterministic validation:

- Reference tests compare against analytic/simple known answers where available.
- Regression tests pin tolerances and verify diagnostics fields.
- Cross-backend consistency tests compare `cpu_reference` vs `cpu_optimized` vs `gpu_*` with documented tolerances.
- Degenerate inputs (zero-area, non-manifold, disconnected components, invalid constraints) must return explicit `MethodError` or `status` + diagnostics.

## Error and fallback model

- All execution entry points return `Expected<Result, MethodError>`.
- Recoverable numerical degradation should be visible in `Result::status` and `Result::diagnostics`.
- Hard failures return `MethodError` with categorized reason (input invalid, solver non-convergence, backend unavailable, resource exhaustion).

## Placement rules

- Method manifests and paper context: `methods/**`.
- Method correctness tests and benchmark manifests: `methods/**` and `benchmarks/**`.
- Engine-layer adapters (if needed): owning subsystem (`src/geometry`, `src/graphics`, `src/runtime`) while preserving architecture invariants.

## Engine integration contract

A production method is integrated only when its task records and proves the
whole consumer path. The required `## Engine integration` matrix contains:

| Field | Required decision |
| --- | --- |
| Least-structured input | The weakest data/topology contract the public method actually needs. |
| Compatible entity sources | Every ECS entity/source satisfying that input contract, not merely the provenance used during development. |
| `RuntimeModule` | Public runtime binding and availability/readiness path. |
| Config/agent | Serializable or command control using the same validated apply path as UI. |
| UI | Discoverability under every appropriate geometric domain, with readiness diagnostics. |
| Publication | Destination entity/domain/property plus explicit topology/cardinality and undo/history behavior. |
| End-to-end tests | Tests that start from each compatible source and exercise runtime binding, publication, and UI discovery. |

The element-domain rows in
[`geometry-api-style.md`](geometry-api-style.md#ecs-element-domain-source-contract)
govern geometry methods. In particular, a point-span method accepts the logical
`Vertices` role of point-cloud, graph, and mesh entities without conversion
(`HARDEN-087` owns the remaining physical graph `Nodes` migration). Requiring
stronger topology is correct only when the public method contract requires it.
Input eligibility and result publication are separate decisions:
a same-cardinality property may publish in place, while a topology/cardinality
change requires an explicit owning operation and must not silently discard
richer source domains.

Method-only scientific slices may defer runtime/config/UI/publication work, but
each deferred matrix row names the task that owns it. A package is not described
as end-to-end or editor-integrated until those rows and their tests close.

## Complexity and diagnostics expectations

Each method package should document:

- Time complexity: expected asymptotic runtime by major stage (e.g., assembly, solve, post-process).
- Space complexity: dominant memory terms.
- Diagnostics schema: fields emitted and interpretation.

This keeps method integration reviewable for both scientific correctness and runtime performance.
