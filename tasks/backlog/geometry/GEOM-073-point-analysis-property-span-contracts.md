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
  keeping `Cloud` overloads as convenience adapters.

## Non-goals

- No numerical or default-policy change, runtime/UI integration, container
  removal, topology mutation, or new estimator/descriptor.
- No generic framework spanning unrelated algorithms.

## Context

- Several read-only or same-cardinality algorithms still require `Cloud` even
  though their implementation consumes positions, optional normals, indices,
  and output arrays. This prevents face-center/edge/halfedge properties from
  being used directly and makes a container wrapper look semantic.
- Re-read each implemented formulation before changing its seam: Rusu et al.'s
  statistical filtering lineage, Zhong's 2009 ISS detector, and Rusu et al.'s
  FPFH (DOI `10.1109/ROBOT.2009.5152473`), plus later robustness/scalability
  improvements. Preserve the current algorithms and record exclusions.

## Spatial acceleration consideration

Preserve the generic property/span boundary so future callers can supply or reuse
a geometry-owned neighborhood index. Radius outliers/ISS/FPFH are candidates;
spacing, splat radii, bilateral/statistical analysis and automatic feature scales
need kNN/self exclusion. The shared point LBVH now supplies these queries
(GPU k=1..64); consumer adapters remain to be integrated. Retain
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
- [ ] Existing `Cloud` callers remain source-compatible and behavior-identical.

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

## Outlier slice progress (2026-09-09)

RUNTIME-209/UI-041 add span statistical/radius analysis plus cached CPU/Vulkan
query execution and canonical-domain publication. Keep this task open for the
remaining statistics/radii, bilateral, density, simplified probability and
feature utilities. Reuse SpatialIndexCache where the estimator semantics fit;
do not conflate LOF-like probability with the statistical/radius masks.

## Density slice progress (2026-09-09)

RUNTIME-220 adds span/supplied-candidate density kernels and canonical-domain
CPU/Vulkan runtime/config/UI publication. The inherited local Gaussian average
and spacing bandwidth remain distinct from full-sample KDE. This task remains
open for statistics/radii, bilateral, simplified probability and features.

## Spacing/radii slice progress

RUNTIME-221 adds span/supplied-candidate statistics and radius estimation, plus
canonical-domain CPU/Vulkan runtime/config/UI radius publication with full
nearest-other spacing diagnostics. Sampled statistics retain deterministic stride;
invalid/nonfinite or unrepresentable float results now fail closed. Keep this
task open for bilateral filtering, simplified probability and feature utilities.
RUNTIME-222 owns model-space radius rendering; do not reinterpret radii as pixels.
