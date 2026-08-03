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
