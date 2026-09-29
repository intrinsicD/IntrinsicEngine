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

## Methods (early slices first; "port" = today's per-iteration CPU stage moved to the GPU)
| Method | Input | Output / preview | CPU stage today | GPU port effort |
|---|---|---|---|---|
| SpatialIndexCache users (index build) | pool (GRAPHICS-154) | index stays GPU-local | none | - |
| Vertex normals (face-weighted, face normals) | pool + face indices | normal ring | everything (CPU only today) | small: face-normal kernel, then a per-vertex gather over a vertex->face CSR kept per topology revision (deterministic, no float atomics) |
| Outliers (statistical, radius, LDR, remove) | pool via LBVH | score/mask ring | reductions on downloaded neighborhoods | small: per-point kNN/radius-count kernels, fixed-order mean/std reduction, stream compaction for removal |
| Kernel density, point spacing, density weights | pool via LBVH | scalar ring | reductions on downloaded neighborhoods | small: one kernel over LBVH neighbors |
| Point-set PCA normals | pool via LBVH | normal ring | covariance and orientation | small for unoriented/viewpoint (closed-form 3x3 eigen); MST orientation stays CPU (parallel Boruvka later) |
| LOP (fully GPU) | pool, stride-12 kernels | position ring, preview every k iterations | none | - |
| k-means | pool, stride-12 kernel | labels ring (colormap preview) | none | - |
| FPS | pool -> double on device | order/mask | none | completion-only submit replaces the interim double |
| PropertyFilter, implicit CG | residency (RUNTIME-292) | typed ring | CG reports (Observe needs Done) | - |
| Keypoints | index views | score/mask ring | none | - |
| Bilateral | pool | position ring | update per pass | small-medium: kernel per pass, GPU LBVH rebuild |
| FPFH | pool + normals | descriptor ring | SPFH/FPFH | medium: two kernels (SPFH per point, weighted FPFH sum) |
| WLOP / CLOP | pool | position ring | projection per iteration | medium: neighbor sums per iteration |
| EAR | pool | position ring | projection + insertion | medium-large: insertion changes the count (compaction, dynamic sizes) |
| ICP | pool | pose | Kabsch per iteration | medium: correspondence sums reduced on the GPU; a matrix up and a convergence value back per iteration |
| CPD rigid/affine | residency (target float-float once) | moving-source preview | M-step sums | medium: M-step sums on the GPU, the tiny SVD on the CPU or a one-thread kernel |
| CPD nonrigid / BCPD | residency | moving-source preview | M-step solve | medium with a basis (METHOD-065: m x k products + k x k solve) |
| Construction (Hoppe) | pool via LBVH | mesh (atomic publish) | distance field + marching cubes | large: variable-size output |
| Progressive Poisson | pool | topology change, atomic publish | none | - |
| Texture bake | residency values | texture | none | - |

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
