# BUG-101 UV-atlas edge-grouping scaling result

This report records a local before/after comparison for
`geometry.uv_atlas.fast_staged_edge_grouping.scaling`. It supports the
BUG-101 algorithmic change; it is not a cross-machine, release-wide, or
unsanitized performance claim.

## Provenance and procedure

- Baseline implementation: detached worktree at
  `8ca524387a92ea444b63fb28cac723ca4c9feebb`, with the pre-fix
  `Geometry.UvAtlas.cpp` unchanged and only the benchmark instrumentation
  applied.
- Candidate measurement snapshot: local revision
  `5ea1b696a503d29d028796b26adb9ed2ba28539e` plus the uncommitted BUG-101
  UV-atlas patch. That local revision was subsequently superseded by the
  test-only `c9f7067a` amend; the measured `Geometry.UvAtlas.cpp` and benchmark
  harness remain byte-identical to the final candidate. The only intervening
  committed geometry change was isolated to point-cloud PLY I/O and does not
  enter the measured UV-atlas path.
- Harness: the benchmark header, implementation, and runner were byte-identical
  between worktrees at collection.
- Build: `ci-vulkan`, Debug, Clang 23.0.0, ASan+UBSan.
- Host: Linux 6.14.0-37-generic x86_64; 11th Gen Intel Core i9-11900KF,
  8 cores / 16 threads.
- Scheduling: baseline then candidate, serialized. Immediately before each
  invocation, no `ninja` or `clang++` process was active.
- Dataset: generated 16x16 and 32x32 indexed planar grids (512 and 2,048
  faces), one warmup pair, then five alternating small/large measurement pairs.
  Each reported runtime is the median of its five raw samples.

The checked-in baseline snapshot is a normalized copy of the emitted baseline
payload: raw samples are unchanged, while its self-comparison fields use the
collected baseline medians. Candidate ratios below are recomputed directly from
the two emitted raw medians.

## Result

| Measurement | `8ca52438` vector-search baseline | BUG-101 candidate | Candidate / baseline |
| --- | ---: | ---: | ---: |
| Small-grid median runtime (ms) | 149.399490 | 116.171666 | 0.777591 |
| Large-grid median runtime (ms) | 1031.895895 | 555.416594 | 0.538249 |
| Runtime scaling ratio (4x faces) | 6.906957 | 4.780999 | 0.692200 |
| Normalized runtime scaling factor | 1.726739 | 1.195250 | 0.692200 |
| Large-grid throughput (faces/s) | 1984.696334 | 3687.322313 | 1.857877 |
| Quality error L2 | 0.0 | 0.0 | unchanged |

Under these controlled local conditions, the observed large-grid median was
46.175% lower and the face-normalized scaling factor was 30.780% lower. The
remaining 1.195 normalized factor includes chart parameterization, packing,
diagnostics, and mesh construction; this benchmark does not attribute all
remaining superlinearity to edge grouping.

## Raw timing evidence

| Revision | Small-grid samples (ms) | Large-grid samples (ms) |
| --- | --- | --- |
| Baseline | 153.47285399999998, 149.39948999999999, 149.06818500000000, 147.73365999999999, 151.37348399999999 | 1034.64980100000002, 1025.39796999999999, 1031.89589499999988, 1026.96054099999992, 1032.03051200000004 |
| Candidate | 116.17166599999999, 115.36147000000000, 115.89651900000000, 118.65119399999999, 122.16015999999999 | 561.33638599999995, 564.52916299999993, 555.32393000000002, 555.41659399999992, 553.33328999999992 |

## Correctness evidence

Both results retained all 2,048 large-fixture faces, produced 1,089 output
vertices, one chart, zero interior seams, 128 boundary seams, finite normalized
UVs, no fallback, and deterministic public atlas output. The baseline and
candidate quality vectors were identical:

```text
[output vertices, output faces, charts, seams, boundary seams,
 uv_min.x, uv_min.y, uv_max.x, uv_max.y,
 mean conformal distortion, max stretch, flipped elements]
[1089, 2048, 1, 0, 128,
 0.0078125, 0.0078125, 0.99218755960464478, 0.91768068075180054,
 1.18278551100503537, 0.70545702589212356, 0]
```

Count and distortion/stretch deltas are normalized by
`max(abs(baseline), 1)`; seam/flipped deltas and UV-bound deltas are
absolute. All 12 candidate deltas were exactly zero, so
`quality_error_l2 = sqrt(sum(delta_i^2)) = 0`. The FNV-like signature over
status/provenance, public topology maps, chart records, seam records, and exact
output-UV bit patterns was `5684639256857304174` for both revisions.

The durable baseline payload is
[`geometry_uv_atlas_fast_staged_edge_grouping_scaling_8ca52438.json`](../baselines/geometry_uv_atlas_fast_staged_edge_grouping_scaling_8ca52438.json).
No timing-ratio gate is attached to this PR-fast smoke benchmark.
