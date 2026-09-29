---
id: RUNTIME-294
theme: I
depends_on: [RUNTIME-293]
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: planned from the operator's GPU residency decision and two independent design reviews (2026-09-29, ADR 0030); implementation owes the contract tests and gpu;vulkan smokes listed below.
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources, method.engine-integration]
---
# RUNTIME-294 — Migrate every GPU method to the property residency

## Goal
- ADR 0030 decisions 8-9.
- Every GPU method records from `GpuPropertyView` inputs into `WriteLease` outputs and commits
  through `CommitGpuProperty`.
- Hybrid methods keep only the per-iteration traffic their CPU stage needs and report it.
- A later slice per method ports that stage.
- Each row becomes its own session-sized slice (its own task when started), with a parity smoke
  against the CPU reference and IO counters.
- Future GPU backends follow the same seam.

## Methods (order by expected benefit; "hybrid" = CPU stage per iteration)
| Method | Input | Output / preview | Remaining per-iteration traffic |
|---|---|---|---|
| SpatialIndexCache users (index build) | pool (GRAPHICS-154) | index stays GPU-local | none |
| LOP (fully GPU) | pool, stride-12 kernels | position ring, preview every k iterations | none |
| k-means | pool, stride-12 kernel | labels ring (colormap preview) | none |
| FPS | pool -> double on device (shared conversion kernels land here) | order/mask | completion-only submit replaces the interim double |
| PropertyFilter, implicit CG | residency (RUNTIME-292) | typed ring | CG reports (Observe needs Done) |
| Keypoints | index views | score/mask ring | none |
| WLOP / CLOP / EAR (hybrid, <= 64 iterations) | pool | position ring | neighborhoods down, moved points up: port projection |
| Normals, outliers, FPFH, density, weights, spacing, bilateral, construction (hybrid) | pool via index | scalar/vec ring | neighborhood downloads: port each reduction |
| CPD E-step (hybrid) | residency (target float-float once) | moving-source preview | E-step sums back, moved source up: GPU M-step to remove |
| ICP (hybrid) | pool | pose only | correspondences back; transform queries on GPU (matrix up) |
| Progressive Poisson | pool | topology change, atomic publish | none |
| Texture bake | residency values | texture | none |

## Engine integration
| Row | Disposition |
|---|---|
| Least-structured input | Unchanged per method: any `GeometryPropertyRef` on a compatible point domain; the resolver checks the domain, not only the reference. |
| Compatible entity sources | Mesh vertices, graph nodes, point clouds; the pool path is used only for row-aligned positions, residency otherwise. |
| RuntimeModule | Unchanged per method. |
| Config/agent | Unchanged backend enums; IO counters added to results and agent output. |
| UI | Panels show "GPU preview" and the IO counters. |
| Publication / cardinality | Through `CommitGpuProperty` (ADR 0030 decision 6); topology changes publish atomically. |
| End-to-end tests | One parity + IO smoke per migrated method. |

## Acceptance criteria
- [ ] Every row migrated or split into its own task with the reason.
- [ ] No method uploads a resident input; the fully GPU methods move data only at start and end.
- [ ] `method.engine-integration` publication rows state "GPU preview: yes/no; commit via X".

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu|vulkan' --timeout 900
```
