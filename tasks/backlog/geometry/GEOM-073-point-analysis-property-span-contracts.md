---
id: GEOM-073
theme: I
depends_on: [GEOM-016, GEOM-017, GEOM-027]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [geometry.element-domain-sources]
maturity_target: CPUContracted
---
# GEOM-073 — Point-analysis property/span contracts

## Goal

- Add generic typed property/span entry points for the existing point-set
  analyses in `Geometry.PointCloud.Utils` and `Geometry.PointCloud.Features`,
  retaining `Cloud` adapters only for actual callers or deleted-slot semantics.

## Non-goals

- No numerical or default-policy change, runtime/UI integration, container
  removal, topology mutation, or new estimator/descriptor.
- No generic framework spanning unrelated algorithms.

## Context

- Span and supplied-neighborhood kernels now cover statistics/radii, outliers,
  density, bilateral filtering, ISS keypoints and FPFH. Descriptor matching
  already consumes `DescriptorSet`; coarse alignment already takes position
  spans. Those are canonical implementations, not remaining porting work.
- The remaining audit starts with `ComputeBoundingBox(const Cloud&)` and
  `ApplyGaussianNoise(Cloud&, ...)` in `Geometry.PointCloud.Utils`. Determine
  whether existing bounds/property helpers already satisfy their contracts;
  add only the missing borrowed-input seam, preserving deleted-slot behavior.
  Count-changing `VoxelDownsample`/`RandomSubsample` remain explicit owning
  operations; their existence alone does not require new span wrappers.
- Framework24 covariance probability and scalar Gaussian saliency are distinct
  feature gaps in the product inventory, not unimplemented overloads of the
  delivered local-distance-ratio or ISS kernels. They are outside this task's
  no-new-estimator scope and remain tracked by REVIEW-004.
- Re-read each implemented formulation before changing its seam: Rusu et al.'s
  statistical filtering lineage, Zhong's 2009 ISS detector, and Rusu et al.'s
  FPFH (DOI `10.1109/ROBOT.2009.5152473`), plus later robustness/scalability
  improvements. Preserve the current algorithms and record exclusions.

## Spatial acceleration consideration

Preserve the generic property/span boundary so future callers can supply or reuse
a geometry-owned neighborhood index. Radius outliers/ISS/FPFH are candidates;
spacing, splat radii, bilateral/statistical analysis and automatic feature scales
need kNN/self exclusion. The shared point LBVH now supplies these queries
(GPU k=1..64); outlier, density, spacing, bilateral, keypoint, FPFH,
projection and point-construction adapters are integrated. Reuse them;
do not reopen their implementation here. Retain
numerical behavior here; do not import Runtime.SpatialIndexCache or add a
universal query interface just for a future backend.

See the [shared spatial-index consumer inventory](../../../docs/architecture/spatial-index-consumers.md).

## Required changes

- [ ] Inventory every `Cloud`-taking utility/feature and classify its real
      inputs, outputs, cardinality, and deleted-slot semantics.
- [ ] Add the smallest span/property overloads for read-only and
      same-cardinality operations; make `Cloud` overloads delegate without
      copying or changing diagnostics.
- [ ] Keep count-changing owning results explicit and separate from runtime
      publication; do not mutate a topology-bearing property set.
- [ ] Use generic `Property<T>`/`ConstProperty<T>` only where a typed property
      is useful; do not expose `VertexProperty` as an eligibility boundary.

## Tests

- [ ] Prove span/property and `Cloud` overload parity for every migrated
      operation, including optional normals/indices and failures.
- [ ] Exercise properties originating from vertex, edge, halfedge, and face
      sets without importing ECS/runtime.
- [ ] Preserve deterministic outputs and existing complexity/diagnostics.

## Docs

- [ ] Update geometry API/point-cloud roadmap docs with the overload inventory,
      cardinality rules, and reviewed literature.

## Acceptance criteria

- [ ] Point-set analyses depend on their typed values, not `Cloud` provenance or
      vertex handles.
- [ ] Update in-tree callers together and preserve behavior. No compatibility
      wrappers or aliases are required for superseded public APIs.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'PointCloud|Features|Outlier|Density' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes

- No ECS/runtime import, unreviewed algorithm substitution, implicit
  topology/cardinality edit, or handle-specific generic API.

## Delivered slices

RUNTIME-209/UI-041 and RUNTIME-220 through RUNTIME-227 delivered the analysis,
span/neighborhood and runtime publication slices above. RUNTIME-228 through
RUNTIME-230 delivered projection and point-construction adapters. Their task
records and current kernels own those details; this task does not repeat their
former remaining-work lists. RUNTIME-222 still owns model-space radius rendering.
