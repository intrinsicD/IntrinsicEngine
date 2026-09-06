# Benchmarking Documentation Index

This section defines how IntrinsicEngine benchmarks are authored, executed, validated, and reviewed.

## Canonical docs

- [Overview](overview.md)
- [Dataset policy](dataset-policy.md)
- [Metrics](metrics.md)
- [Baselines](baselines.md)
- [CI policy](ci-policy.md)
- [Report template](report-template.md)
- [Benchmark manifest schema](benchmark-manifest-schema.md)
- [Benchmark result JSON schema](result-json-schema.md)

## Regression diagnostics

- [UV atlas remap allocation regression](bug159-atlas-remap-diagnostics.md)

## Related process docs

- [Agent benchmark workflow](../agent/benchmark-workflow.md)
- [Agent workflow evidence and experiment custody](../agent/workflow-evidence.md)
- [Benchmarks root README](../../benchmarks/README.md)
- [Benchmark review checklist](../agent/benchmark-workflow.md) (§"Review checklist")

## Local feature-boundary mesh comparison

`IntrinsicCurvatureBoundaryMesh` is an opt-in CPU runner built with the `ci`
preset. [`curvature_boundary_cohort.py`](../../benchmarks/runners/curvature_boundary_cohort.py)
runs the fixed local OBJ cohort with bounded processes, per-input/source/output
hashes, explicit implementation identity, and retained load/solver failures.
Its `curvature_*_mesh_local_cohort.yaml` manifests define the experimental
profiles; thresholds check completeness, hard constraints, and bounded health,
not semantic segmentation quality. See the
[method experiment](../../methods/geometry/curvature_segmentation/boundary_partition_experiment.md)
for objectives and limits.

[`curvature_boundary_viewer.py`](../../benchmarks/runners/curvature_boundary_viewer.py)
uses the adjacent HTML template to embed validated results into a standalone,
network-free comparison page. Keep generated pages under a local build/output
directory: they contain the private input geometry. The generator checks exact
loaded-mesh exports, labels, edge boundaries, source/output hashes, and region
areas before displaying a run. Rejected inputs remain visible.

```bash
cmake --build --preset ci --target IntrinsicCurvatureBoundaryMesh
python3 benchmarks/runners/curvature_boundary_cohort.py \
  --runner build/ci/bin/IntrinsicCurvatureBoundaryMesh \
  --dataset-root /path/to/local/obj --output /tmp/curvature-cohort \
  --modes local clean_medium clean_coarse curves
python3 benchmarks/runners/curvature_boundary_viewer.py \
  --cohort /tmp/curvature-cohort --output build/curvature-review/index.html
```

The docs/tooling CI gate runs
`tests/regression/tooling/Test.CurvatureBoundaryViewer.py` on synthetic meshes,
including native vertex reordering, negative OBJ indices, and rejected
identity/hash/label/boundary mismatches. External datasets are not CI inputs.

The [overnight report](../../methods/geometry/curvature_segmentation/boundary_partition_report.md)
links all 48 final outcomes, including twelve rejected-input runs, and the
36 non-claim-eligible canonical observations.
